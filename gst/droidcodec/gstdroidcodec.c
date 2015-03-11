/*
 * gst-droid
 *
 * Copyright (C) 2014-2015 Mohammed Sameer <msameer@foolab.org>
 * Copyright (C) 2015 Jolla LTD.
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

GST_DEBUG_CATEGORY_EXTERN (gst_droid_codec_debug);
#define GST_CAT_DEFAULT gst_droid_codec_debug

static GstBuffer *create_mpeg4venc_codec_data (DroidMediaData * data);
static GstBuffer *create_h264enc_codec_data (DroidMediaData * data);
static gboolean create_mpeg4vdec_codec_data (GstDroidCodec * codec,
    GstBuffer * data, DroidMediaData * out);
static gboolean create_h264dec_codec_data (GstDroidCodec * codec,
    GstBuffer * data, DroidMediaData * out);
static gboolean process_h264dec_data (GstDroidCodec * codec, GstBuffer * buffer,
    DroidMediaData * out);
static gboolean is_mpeg4v (const GstStructure * s);
static gboolean is_h264_dec (const GstStructure * s);
static gboolean is_h264_enc (const GstStructure * s);
static void h264enc_complement (GstCaps * caps);
static gboolean process_h264enc_data (DroidMediaData * in,
    DroidMediaData * out);
static void gst_droid_codec_release_input_frame (void *data);
static void gst_droid_codec_free (GstDroidCodec * codec);

GST_DEFINE_MINI_OBJECT_TYPE (GstDroidCodec, gst_droid_codec);

typedef struct
{
  gpointer data;
  GstVideoCodecFrame *frame;
} GstDroidCodecFrameReleaseData;

struct _GstDroidCodecPrivate
{
  guint h264_nal;
};

struct _GstDroidCodecInfo
{
  GstDroidCodecType type;
  const gchar *mime;
  const gchar *droid;
  const gchar *caps;

    gboolean (*validate_structure) (const GstStructure * s);
  void (*complement_caps) (GstCaps * caps);
  GstBuffer *(*create_encoder_codec_data) (DroidMediaData * data);
    gboolean (*process_encoder_data) (DroidMediaData * in,
      DroidMediaData * out);
    gboolean (*create_decoder_codec_data) (GstDroidCodec * codec,
      GstBuffer * data, DroidMediaData * out);
    gboolean (*process_decoder_data) (GstDroidCodec * codec, GstBuffer * buffer,
      DroidMediaData * out);
};

/* codecs */
#define CAPS_FRAGMENT \
  " , width = (int) [1, MAX], height = (int)[1, MAX], framerate = (fraction)[1/MAX, MAX]"

static GstDroidCodecInfo codecs[] = {
  /* decoders */
  {GST_DROID_CODEC_DECODER, "video/mpeg", "video/mp4v-es",
        "video/mpeg, mpegversion=4" CAPS_FRAGMENT,
      is_mpeg4v, NULL, NULL, NULL, create_mpeg4vdec_codec_data, NULL},

  {GST_DROID_CODEC_DECODER, "video/x-h264", "video/avc",
        "video/x-h264, stream-format=avc,alignment=au" CAPS_FRAGMENT,
        is_h264_dec, NULL, NULL, NULL,
      create_h264dec_codec_data, process_h264dec_data},

  {GST_DROID_CODEC_DECODER, "video/x-h263", "video/3gpp",
        "video/x-h263" CAPS_FRAGMENT, NULL,
      NULL, NULL, NULL, NULL, NULL},

  /* encoders */
  {GST_DROID_CODEC_ENCODER, "video/mpeg", "video/mp4v-es",
        "video/mpeg, mpegversion=4, systemstream=false" CAPS_FRAGMENT,
      is_mpeg4v, NULL, create_mpeg4venc_codec_data, NULL, NULL, NULL},

  {GST_DROID_CODEC_ENCODER, "video/x-h264", "video/avc",
        "video/x-h264, stream-format=avc,alignment=au" CAPS_FRAGMENT,
        is_h264_enc, h264enc_complement, create_h264enc_codec_data,
      process_h264enc_data, NULL, NULL},
};

GstDroidCodec *
gst_droid_codec_new_from_caps (GstCaps * caps, GstDroidCodecType type)
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

    if (!codecs[x].validate_structure || codecs[x].validate_structure (s)) {
      GstDroidCodec *codec = g_slice_new (GstDroidCodec);
      codec->info = &codecs[x];
      codec->data = g_slice_new0 (GstDroidCodecPrivate);

      gst_mini_object_init (GST_MINI_OBJECT_CAST (codec), 0,
          gst_droid_codec_get_type (), NULL, NULL,
          (GstMiniObjectFreeFunction) gst_droid_codec_free);
      return codec;
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

const gchar *
gst_droid_codec_get_droid_type (GstDroidCodec * codec)
{
  return codec->info->droid;
}

void
gst_droid_codec_free (GstDroidCodec * codec)
{
  g_slice_free (GstDroidCodecPrivate, codec->data);
  g_slice_free (GstDroidCodec, codec);
}

void
gst_droid_codec_complement_caps (GstDroidCodec * codec, GstCaps * caps)
{
  if (codec->info->complement_caps)
    codec->info->complement_caps (caps);
}

GstBuffer *
gst_droid_codec_create_encoder_codec_data (GstDroidCodec * codec,
    DroidMediaData * data)
{
  return codec->info->create_encoder_codec_data (data);
}

gboolean
gst_droid_codec_create_decoder_codec_data (GstDroidCodec * codec,
    GstBuffer * data, DroidMediaData * out)
{
  return codec->info->create_decoder_codec_data (codec, data, out);
}

gboolean
gst_droid_codec_prepare_decoder_frame (GstDroidCodec * codec,
    GstVideoCodecFrame * frame, DroidMediaData * data,
    DroidMediaBufferCallbacks * cb)
{
  GstDroidCodecFrameReleaseData *release_data;

  /*
   * We have multiple cases.
   * H264 nal prefix size 4 -> map the buffer writable, fix up and proceed
   * H264 nal prefix size != 4 -> copy data and fix up.
   * The rest -> map buffer read only and proceed
   * However as I do not believe that the memcpy() is that expensive
   * so we will just copy everything and optimize later if we find issues.
   */

  if (codec->info->process_decoder_data) {
    if (!codec->info->process_decoder_data (codec, frame->input_buffer, data)) {
      return FALSE;
    }
  } else {
    data->size = gst_buffer_get_size (frame->input_buffer);
    data->data = g_malloc (data->size);
    gst_buffer_extract (frame->input_buffer, 0, data->data, data->size);
  }

  release_data = g_slice_new (GstDroidCodecFrameReleaseData);

  release_data->data = data->data;
  release_data->frame = gst_video_codec_frame_ref (frame);

  cb->unref = gst_droid_codec_release_input_frame;
  cb->data = release_data;

  return TRUE;
}

GstBuffer *
gst_droid_codec_prepare_encoded_data (GstDroidCodec * codec,
    DroidMediaData * in)
{
  GstBuffer *buffer;

  if (codec->info->process_encoder_data) {
    DroidMediaData out;
    if (!codec->info->process_encoder_data (in, &out)) {
      buffer = NULL;
    } else {
      buffer = gst_buffer_new_wrapped (out.data, out.size);
    }
  } else {
    buffer = gst_buffer_new_allocate (NULL, in->size, NULL);
    gst_buffer_fill (buffer, 0, in->data, in->size);
  }

  return buffer;
}

static GstBuffer *
create_mpeg4venc_codec_data (DroidMediaData * data)
{
  GstBuffer *codec_data = gst_buffer_new_allocate (NULL, data->size, NULL);

  gst_buffer_fill (codec_data, 0, data->data, data->size);

  GST_BUFFER_OFFSET (codec_data) = 0;
  GST_BUFFER_OFFSET_END (codec_data) = 0;
  GST_BUFFER_PTS (codec_data) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DTS (codec_data) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (codec_data) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_FLAG_SET (codec_data, GST_BUFFER_FLAG_HEADER);

  return codec_data;
}

static GstBuffer *
create_h264enc_codec_data (DroidMediaData * data)
{
  GstH264NalParser *parser = gst_h264_nal_parser_new ();
  gsize offset = 0;
  GSList *sps = NULL, *pps = NULL;
  gsize sps_size = 0, pps_size = 0;
  gint num_sps = 0, num_pps = 0;
  GstByteWriter *writer = NULL;
  guint8 profile_idc = 0, profile_comp = 0, level_idc = 0;
  gboolean idc_found = FALSE;
  GstBuffer *codec_data = NULL;
  GstH264NalUnit nal;
  GstH264ParserResult res;
  int x;

  res =
      gst_h264_parser_identify_nalu (parser, data->data, offset, data->size,
      &nal);

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

    res =
        gst_h264_parser_identify_nalu (parser, data->data, offset, data->size,
        &nal);
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
  gst_byte_writer_put_uint8 (writer, 1);        /* AVC decoder configuration version 1 */
  gst_byte_writer_put_uint8 (writer, profile_idc);      /* profile idc */
  gst_byte_writer_put_uint8 (writer, profile_comp);     /* profile compatibility */
  gst_byte_writer_put_uint8 (writer, level_idc);        /* level idc */
  gst_byte_writer_put_uint8 (writer, (0xfc | (4 - 1))); /* nal length size - 1 */
  gst_byte_writer_put_uint8 (writer, 0xe0 | num_sps);   /* number of sps */

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

  gst_byte_writer_put_uint8 (writer, num_pps);  /* number of pps */

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

  codec_data = gst_byte_writer_free_and_get_buffer (writer);
  writer = NULL;

out:
  if (sps) {
    g_slist_free_full (sps, (GDestroyNotify) gst_buffer_unref);
  }

  if (pps) {
    g_slist_free_full (pps, (GDestroyNotify) gst_buffer_unref);
  }

  if (parser) {
    gst_h264_nal_parser_free (parser);
  }

  return codec_data;
}

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
      && !g_strcmp0 (format, "avc");
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
h264enc_complement (GstCaps * caps)
{
  gst_caps_set_simple (caps, "alignment", G_TYPE_STRING, "au",
      "stream-format", G_TYPE_STRING, "avc", NULL);
}

static gboolean
create_mpeg4vdec_codec_data (GstDroidCodec * codec G_GNUC_UNUSED,
    GstBuffer * data, DroidMediaData * out)
{
  /*
   * If there are things which I hate the most, this function will be among them
   * http://xhelmboyx.tripod.com/formats/mp4-layout.txt
   */
  GstMapInfo info;
  GstByteWriter *writer = NULL;
  codec->data->h264_nal = 0;

  if (!gst_buffer_map (data, &info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return FALSE;
  }

  writer = gst_byte_writer_new_with_size (info.size + 29 - 4, FALSE);
#if 0                           /* stagefright ESDS parser does not like us when we have the first 4 bytes */
  gst_byte_writer_put_uint32_be (writer, 0);    /* version and flags */
#endif
  gst_byte_writer_put_uint8 (writer, 0x03);     /* ES descriptor type tag */
  gst_byte_writer_put_uint8 (writer, 23 + info.size);   /* size */
  gst_byte_writer_put_uint16_be (writer, 0);    /* ES ID */
  gst_byte_writer_put_uint8 (writer, 0x1f);     /* priority */
  gst_byte_writer_put_uint8 (writer, 0x04);     /* decoder config descriptor type tag */
  gst_byte_writer_put_uint8 (writer, 15 + info.size);
  gst_byte_writer_put_uint8 (writer, 0x20);     /* object type: MPEG4 video */
  gst_byte_writer_put_uint8 (writer, 0x11);     /* stream type: visual stream */

  /* buffer size db, average bit rate and max bit rate values are taken from stagefright MPEG4Writer */
  gst_byte_writer_put_uint8 (writer, 0x01);     /* buffer size db: 1/3 */
  gst_byte_writer_put_uint8 (writer, 0x77);     /* buffer size db: 2/3 */
  gst_byte_writer_put_uint8 (writer, 0x00);     /* buffer size db: 3/3 */
  gst_byte_writer_put_uint8 (writer, 0x00);     /* max bit rate: 1/4 */
  gst_byte_writer_put_uint8 (writer, 0x03);     /* max bit rate: 2/4 */
  gst_byte_writer_put_uint8 (writer, 0xe8);     /* max bit rate: 3/4 */
  gst_byte_writer_put_uint8 (writer, 0x00);     /* max bit rate: 4/4 */

  gst_byte_writer_put_uint8 (writer, 0x00);     /* average bit rate: 1/4 */
  gst_byte_writer_put_uint8 (writer, 0x03);     /* average bit rate: 2/4 */
  gst_byte_writer_put_uint8 (writer, 0xe8);     /* average bit rate: 3/4 */
  gst_byte_writer_put_uint8 (writer, 0x00);     /* average bit rate: 4/4 */

  gst_byte_writer_put_uint8 (writer, 0x05);     /* decoder specific descriptor type tag */
  gst_byte_writer_put_uint8 (writer, info.size);        /* size */
  gst_byte_writer_put_data (writer, info.data, info.size);      /* codec data */

  gst_byte_writer_put_uint8 (writer, 0x06);     /* SL config descriptor type tag */
  gst_byte_writer_put_uint8 (writer, 0x01);     /* descriptor type length */
  gst_byte_writer_put_uint8 (writer, 0x02);     /* SL value */

  out->size = gst_byte_writer_get_size (writer);
  out->data = gst_byte_writer_free_and_get_data (writer);

  gst_buffer_unmap (data, &info);

  return TRUE;
}

static gboolean
create_h264dec_codec_data (GstDroidCodec * codec, GstBuffer * data,
    DroidMediaData * out)
{
  GstMapInfo info;
  gboolean ret = FALSE;

  if (!gst_buffer_map (data, &info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return FALSE;
  }

  if (info.size < 7 || info.data[0] != 1) {
    GST_ERROR ("malformed codec_data");
    goto out;
  }

  codec->data->h264_nal = (1 + (info.data[4] & 3));

  GST_INFO ("nal prefix length %d", codec->data->h264_nal);

  out->size = info.size;
  out->data = g_malloc (info.size);
  memcpy (out->data, info.data, info.size);
  ret = TRUE;

out:
  gst_buffer_unmap (data, &info);

  return ret;
}

static gboolean
process_h264dec_data (GstDroidCodec * codec, GstBuffer * buffer,
    DroidMediaData * out)
{
  GstMapInfo info;
  gboolean ret = FALSE;
  GstByteReader reader;
  GstByteWriter *writer = NULL;

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return FALSE;
  }

  if (info.size < codec->data->h264_nal) {
    GST_ERROR ("malformed data");
    goto out;
  }

  /* initial validation */
  switch (codec->data->h264_nal) {
    case 4:
    case 2:
    case 3:
    case 1:
      break;

    default:
      GST_ERROR ("unhandled nal prefix size %d", codec->data->h264_nal);
      goto out;
  }

  gst_byte_reader_init (&reader, info.data, info.size);

  /* info.size is our best estimate and should be correct if nal prefix is 4 bytes which is most of the cases */
  writer = gst_byte_writer_new_with_size (info.size, FALSE);

  while (gst_byte_reader_get_pos (&reader) < info.size) {
    guint len = 0;
    guint16 len16 = 0;
    guint8 len8 = 0;
    gboolean success = FALSE;
    const guint8 *data = NULL;

    switch (codec->data->h264_nal) {
      case 4:
        success = gst_byte_reader_get_uint32_be (&reader, &len);
        break;

      case 3:
        success = gst_byte_reader_get_uint24_be (&reader, &len);
        break;

      case 2:
        success = gst_byte_reader_get_uint16_be (&reader, &len16);
        len = len16;
        break;

      case 1:
        success = gst_byte_reader_get_uint8 (&reader, &len8);
        len = len8;
        break;

      default:
        g_assert_not_reached ();
        break;
    }

    if (!success) {
      GST_ERROR ("malformed NAL");
      goto out;
    }

    if (!gst_byte_writer_put_data (writer, (guint8 *) "\x00\x00\x00\x01", 4)) {
      GST_ERROR ("failed to write NAL prefix");
      goto out;
    }

    if (!gst_byte_reader_get_data (&reader, len, &data)) {
      GST_ERROR ("failed to read NAL");
      goto out;
    }

    if (!gst_byte_writer_put_data (writer, data, len)) {
      GST_ERROR ("failed to write NAL");
      goto out;
    }

    GST_LOG ("parsed nal unit of size %d", len);
  }

  out->size = gst_byte_writer_get_size (writer);
  out->data = gst_byte_writer_free_and_get_data (writer);
  writer = NULL;
  ret = TRUE;

out:
  if (writer) {
    gst_byte_writer_free (writer);
    writer = NULL;
  }

  gst_buffer_unmap (buffer, &info);

  return ret;
}

static gboolean
process_h264enc_data (DroidMediaData * in, DroidMediaData * out)
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

static void
gst_droid_codec_release_input_frame (void *data)
{
  GstDroidCodecFrameReleaseData *info = (GstDroidCodecFrameReleaseData *) data;

  gst_video_codec_frame_unref (info->frame);
  g_free (info->data);

  g_slice_free (GstDroidCodecFrameReleaseData, info);
}
