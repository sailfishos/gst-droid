/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstdroidcodec.h"
#include "plugin.h"
#include <glib.h>
#include <gst/base/gstbytewriter.h>
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include <gst/codecparsers/gsth264parser.h>

GST_DEFINE_MINI_OBJECT_TYPE (GstDroidCodec, gst_droid_codec);

GST_DEBUG_CATEGORY_EXTERN (gst_droid_codec_debug);
#define GST_CAT_DEFAULT gst_droid_codec_debug

typedef struct
{
  GstBuffer *buffer;
  GstMapInfo info;
  GstVideoCodecFrame *frame;
} DroidBufferCallbackMapInfo;

static void
gst_droid_codec_release_buffer (void *data)
{
  DroidBufferCallbackMapInfo *info = (DroidBufferCallbackMapInfo *) data;

  GST_DEBUG ("release buffer");
  gst_buffer_unmap (info->buffer, &info->info);
  gst_buffer_unref (info->buffer);

  /* We need to release the input buffer */
  gst_buffer_unref (info->frame->input_buffer);
  info->frame->input_buffer = NULL;

  gst_video_codec_frame_unref (info->frame);
  g_slice_free(DroidBufferCallbackMapInfo, info);
}

gboolean
gst_droid_codec_consume_frame (DroidMediaCodec * codec,
    GstVideoCodecFrame * frame, GstClockTime ts)
{
  DroidMediaCodecData data;
  GstMapInfo info;
  DroidMediaBufferCallbacks cb;
  DroidBufferCallbackMapInfo *buffer_data;

  GST_DEBUG ("consume frame");

  data.sync = GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame) ? true : false;
  data.ts = GST_TIME_AS_USECONDS(ts);

  if (!gst_buffer_map (frame->input_buffer, &info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return FALSE;
  }

  data.data.size = info.size;
  data.data.data = info.data;

  GST_LOG ("Consuming frame of size %d", data.data.size);

  buffer_data = g_slice_new (DroidBufferCallbackMapInfo);

  cb.unref = gst_droid_codec_release_buffer;
  cb.data = buffer_data;

  buffer_data->buffer = gst_buffer_ref (frame->input_buffer);
  buffer_data->info = info;
  buffer_data->frame = frame; /* We have a ref already */

  droid_media_codec_write (codec, &data, &cb);

  GST_DEBUG ("frame consumed");

  return TRUE;
}

/* codecs */
#define CAPS_FRAGMENT " , width = (int) [1, MAX], height = (int)[1, MAX], framerate = (fraction)[1/MAX, MAX]"

static gboolean
is_mpeg4v (const GstStructure * s)
{
  gint val;

  return gst_structure_get_int (s, "mpegversion", &val) && val == 4;
}

static gboolean
is_h264_dec (const GstStructure * s)
{
  const char *alignment = gst_structure_get_string (s, "alignment");
  const char *format = gst_structure_get_string (s, "stream-format");

  /* Enforce alignment and format */
  return alignment && format && !g_strcmp0 (alignment, "au")
      && !g_strcmp0 (format, "byte-stream");
}

static gboolean
is_h264_enc (const GstStructure * s)
{
  const char *alignment = gst_structure_get_string (s, "alignment");
  const char *format = gst_structure_get_string (s, "stream-format");

  /* We can accept caps without alignment or format and will add them later on */
  if (alignment && g_strcmp0 (alignment, "au")) {
    return FALSE;
  }

  if (format && g_strcmp0 (format, "avc")) {
    return FALSE;
  }

  return TRUE;
}

static void
h264_compliment (GstCaps * caps)
{
  gst_caps_set_simple (caps, "alignment", G_TYPE_STRING, "au",
      "stream-format", G_TYPE_STRING, "avc", NULL);
}

static gboolean
construct_normal_codec_data (gpointer data, gsize size, GstBuffer **buffer)
{
  GstBuffer *codec_data = gst_buffer_new_allocate (NULL, size, NULL);

  gst_buffer_fill (codec_data, 0, data, size);

  GST_BUFFER_OFFSET (codec_data) = 0;
  GST_BUFFER_OFFSET_END (codec_data) = 0;
  GST_BUFFER_PTS (codec_data) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DTS (codec_data) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (codec_data) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_FLAG_SET (codec_data, GST_BUFFER_FLAG_HEADER);

  *buffer = codec_data;

  return TRUE;
}

static gboolean
construct_h264_codec_data (gpointer data, gsize size, GstBuffer **buffer)
{
  GstH264NalParser *parser = gst_h264_nal_parser_new ();
  gsize offset = 0;
  gboolean ret = FALSE;
  GSList *sps = NULL, *pps = NULL;
  gsize sps_size = 0, pps_size = 0;
  guint8 num_sps = 0, num_pps = 0;
  GstByteWriter *writer = NULL;
  guint8 profile_idc = 0, profile_comp = 0, level_idc = 0;
  gboolean idc_found = FALSE;
  GstH264NalUnit nal;
  GstH264ParserResult res;
  int x;

  res = gst_h264_parser_identify_nalu (parser, data, offset, size, &nal);

  while (res == GST_H264_PARSER_OK || res == GST_H264_PARSER_NO_NAL_END) {
    GstBuffer *buffer = gst_buffer_new_allocate (NULL, nal.size, NULL);
    gst_buffer_fill (buffer, 0, nal.data + nal.offset, nal.size);

    offset += (nal.size + nal.offset);

    if (nal.type == GST_H264_NAL_SPS) {
      if (nal.size >= 4 && !idc_found) {
	idc_found = TRUE;

	profile_idc = nal.data[1];
	profile_comp = nal.data[2];
	level_idc = nal.data[3];
      } else if (nal.size >= 4) {
	if (profile_idc != nal.data[1] || profile_comp != nal.data[2] ||
	    level_idc != nal.data[3]) {
	  GST_ERROR ("Inconsistency in SPS");
	  goto out;
	}
      } else {
	GST_ERROR ("malformed SPS");
	goto out;
      }

      GST_MEMDUMP ("Found SPS", nal.data + nal.offset, nal.size);

      sps = g_slist_append (sps, buffer);
      sps_size += (nal.size + 2);
    } else if (nal.type == GST_H264_NAL_PPS) {
      GST_MEMDUMP ("Found PPS", nal.data + nal.offset, nal.size);
      pps = g_slist_append (pps, buffer);
      pps_size += (nal.size + 2);
    } else {
      GST_LOG ("NAL is neither SPS nor PPS");
      gst_buffer_unref (buffer);
    }

    if (gst_h264_parser_parse_nal (parser, &nal) != GST_H264_PARSER_OK) {
      GST_ERROR ("malformed NAL");
      goto out;
    }

    res = gst_h264_parser_identify_nalu (parser, data, offset, size, &nal);
  }

  if (G_UNLIKELY (!idc_found)) {
    GST_ERROR ("missing codec parameters");
    goto out;
  }

  num_sps = g_slist_length (sps);
  if (G_UNLIKELY (num_sps < 1 || num_sps >= GST_H264_MAX_SPS_COUNT)) {
    GST_ERROR ("No SPS found");
    goto out;
  }

  num_pps = g_slist_length (pps);
  if (G_UNLIKELY (num_pps < 1 || num_pps >= GST_H264_MAX_PPS_COUNT)) {
    GST_ERROR ("No PPS found");
    goto out;
  }

  GST_INFO ("SPS found: %d, PPS found: %d", num_sps, num_pps);

  writer = gst_byte_writer_new_with_size (sps_size + pps_size + 7, FALSE);
  gst_byte_writer_put_uint8 (writer, 1); /* AVC decoder configuration version 1 */
  gst_byte_writer_put_uint8 (writer, profile_idc); /* profile idc */
  gst_byte_writer_put_uint8 (writer, profile_comp); /* profile compatibility */
  gst_byte_writer_put_uint8 (writer, level_idc); /* level idc */
  gst_byte_writer_put_uint8 (writer, (0xfc | (4 - 1))); /* nal length size - 1 */
  gst_byte_writer_put_uint8 (writer, 0xe0 | num_sps);/* number of sps */

  /* SPS */
  for (x = 0; x < num_sps; x++) {
    GstBuffer *buf = g_slist_nth_data (sps, x);
    GstMapInfo info;
    gst_buffer_map (buf, &info, GST_MAP_READ);
    gst_byte_writer_put_uint8 (writer, info.size >> 8);
    gst_byte_writer_put_uint8 (writer, info.size & 0xff);
    gst_byte_writer_put_data (writer, info.data, info.size);
    gst_buffer_unmap (buf, &info);
  }

  gst_byte_writer_put_uint8 (writer, num_pps);/* number of pps */

  /* PPS */
  for (x = 0; x < num_pps; x++) {
    GstBuffer *buf = g_slist_nth_data (pps, x);
    GstMapInfo info;
    gst_buffer_map (buf, &info, GST_MAP_READ);
    gst_byte_writer_put_uint8 (writer, info.size >> 8);
    gst_byte_writer_put_uint8 (writer, info.size & 0xff);
    gst_byte_writer_put_data (writer, info.data, info.size);
    gst_buffer_unmap (buf, &info);
  }

  *buffer = gst_byte_writer_free_and_get_buffer (writer);
  writer = NULL;
  ret = TRUE;

out:
  if (sps) {
    g_slist_free_full (sps, (GDestroyNotify)gst_buffer_unref);
  }

  if (pps) {
    g_slist_free_full (pps, (GDestroyNotify)gst_buffer_unref);
  }

  if (parser) {
    gst_h264_nal_parser_free (parser);
  }

  return ret;
}

static gboolean
construct_h264_data (DroidMediaData *in, DroidMediaData *out)
{
  guint32 size;
  guint8 *data;

  if (in->size >= 4 && memcmp (in->data, "\x00\x00\x00\x01", 4) == 0) {
    /* We will replace the first 4 bytes with the NAL size */
    out->size = in->size;
    data = in->data + 4;
  } else {
    /* We don't have the NAL prefix so we add 4 bytes for the NAL size */
    out->size = in->size + 4;
    data = in->data;
  }

  size = GUINT32_TO_BE (out->size - 4);
  out->data = g_malloc (out->size);
  if (!out->data) {
    return FALSE;
  }

  memcpy (out->data, &size, sizeof (size));
  memcpy (out->data + 4, data, out->size - 4);

  return TRUE;
}

static gboolean
construct_mpeg4_esds (GstBuffer *data, DroidMediaData *out)
{
  /*
   * If there are things which I hate the most, this function will be among them
   * http://xhelmboyx.tripod.com/formats/mp4-layout.txt
   */
  GstMapInfo info;
  GstByteWriter *writer = NULL;

  if (!gst_buffer_map (data, &info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return FALSE;
  }

  writer = gst_byte_writer_new_with_size (info.size + 29 - 4, FALSE);
#if 0 /* stagefright ESDS parser does not like us when we have the first 4 bytes */
  gst_byte_writer_put_uint32_be (writer, 0); /* version and flags */
#endif
  gst_byte_writer_put_uint8 (writer, 0x03); /* ES descriptor type tag */
  gst_byte_writer_put_uint8 (writer, 23 + info.size); /* size */
  gst_byte_writer_put_uint16_be (writer, 0); /* ES ID */
  gst_byte_writer_put_uint8 (writer, 0x1f); /* priority */
  gst_byte_writer_put_uint8 (writer, 0x04); /* decoder config descriptor type tag */
  gst_byte_writer_put_uint8 (writer, 15 + info.size);
  gst_byte_writer_put_uint8 (writer, 0x20); /* object type: MPEG4 video */
  gst_byte_writer_put_uint8 (writer, 0x11); /* stream type: visual stream */

  /* buffer size db, average bit rate and max bit rate values are taken from stagefright MPEG4Writer */
  gst_byte_writer_put_uint8 (writer, 0x01); /* buffer size db: 1/3 */
  gst_byte_writer_put_uint8 (writer, 0x77); /* buffer size db: 2/3 */
  gst_byte_writer_put_uint8 (writer, 0x00); /* buffer size db: 3/3 */
  gst_byte_writer_put_uint8 (writer, 0x00); /* max bit rate: 1/4 */
  gst_byte_writer_put_uint8 (writer, 0x03); /* max bit rate: 2/4 */
  gst_byte_writer_put_uint8 (writer, 0xe8); /* max bit rate: 3/4 */
  gst_byte_writer_put_uint8 (writer, 0x00); /* max bit rate: 4/4 */

  gst_byte_writer_put_uint8 (writer, 0x00); /* average bit rate: 1/4 */
  gst_byte_writer_put_uint8 (writer, 0x03); /* average bit rate: 2/4 */
  gst_byte_writer_put_uint8 (writer, 0xe8); /* average bit rate: 3/4 */
  gst_byte_writer_put_uint8 (writer, 0x00); /* average bit rate: 4/4 */

  gst_byte_writer_put_uint8 (writer, 0x05); /* decoder specific descriptor type tag */
  gst_byte_writer_put_uint8 (writer, info.size); /* size */
  gst_byte_writer_put_data (writer, info.data, info.size);     /* codec data */

  gst_byte_writer_put_uint8 (writer, 0x06); /* SL config descriptor type tag */
  gst_byte_writer_put_uint8 (writer, 0x01); /* descriptor type length */
  gst_byte_writer_put_uint8 (writer, 0x02); /* SL value */

  out->size = gst_byte_writer_get_size (writer);
  out->data = gst_byte_writer_free_and_get_data (writer);

  gst_buffer_unmap (data, &info);

  return TRUE;
}

static GstDroidCodec codecs[] = {
  /* decoders */
  {GST_DROID_CODEC_DECODER, "video/mpeg", "video/mp4v-es", is_mpeg4v, NULL,
   "video/mpeg, mpegversion=4"CAPS_FRAGMENT, NULL, NULL, construct_mpeg4_esds},
  {GST_DROID_CODEC_DECODER, "video/x-h264", "video/avc", is_h264_dec,
   NULL, "video/x-h264, alignment=au, stream-format=byte-stream"CAPS_FRAGMENT, NULL, NULL, NULL},
  {GST_DROID_CODEC_DECODER, "video/x-h263", "video/3gpp", NULL,
   NULL, "video/x-h263"CAPS_FRAGMENT, NULL, NULL, NULL},

  /* encoders */
  {GST_DROID_CODEC_ENCODER, "video/mpeg", "video/mp4v-es", is_mpeg4v,
      NULL, "video/mpeg, mpegversion=4, systemstream=false"CAPS_FRAGMENT,
   construct_normal_codec_data, NULL, NULL},
  {GST_DROID_CODEC_ENCODER, "video/x-h264", "video/avc",
        is_h264_enc, h264_compliment,
   "video/x-h264, alignment=au, stream-format=avc"CAPS_FRAGMENT,
   construct_h264_codec_data, construct_h264_data, NULL},
};

GstDroidCodec *
gst_droid_codec_get_from_caps (GstCaps * caps, GstDroidCodecType type)
{
  int x = 0;
  int len = G_N_ELEMENTS (codecs);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *name = gst_structure_get_name (s);

  for (x = 0; x < len; x++) {
    if (codecs[x].type != type) {
      continue;
    }

    gboolean is_equal = g_strcmp0 (codecs[x].mime, name) == 0;
    if (!is_equal) {
      continue;
    }

    if (!codecs[x].verify || codecs[x].verify (s)) {
      return &codecs[x];
    }
  }

  return NULL;
}

GstCaps *
gst_droid_codec_get_all_caps (GstDroidCodecType type)
{
  GstCaps *caps = gst_caps_new_empty ();
  int x = 0;
  int len = G_N_ELEMENTS (codecs);

  for (x = 0; x < len; x++) {
    if (codecs[x].type != type) {
      continue;
    }

    GstStructure *s = gst_structure_new_from_string (codecs[x].caps);
    caps = gst_caps_merge_structure (caps, s);
  }

  GST_INFO ("caps %" GST_PTR_FORMAT, caps);

  return caps;
}
