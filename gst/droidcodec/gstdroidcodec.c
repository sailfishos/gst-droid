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

  if (format && g_strcmp0 (format, "byte-stream")) {
    return FALSE;
  }

  return TRUE;
}

static void
h264_compliment (GstCaps * caps)
{
  gst_caps_set_simple (caps, "alignment", G_TYPE_STRING, "au",
      "stream-format", G_TYPE_STRING, "byte-stream", NULL);
}

static GstDroidCodec codecs[] = {
  /* decoders */
  {GST_DROID_CODEC_DECODER, "video/mpeg", "video/mp4v-es", is_mpeg4v, NULL,
      "video/mpeg, mpegversion=4"CAPS_FRAGMENT, FALSE},
  {GST_DROID_CODEC_DECODER, "video/x-h264", "video/avc", is_h264_dec,
      NULL, "video/x-h264, alignment=au, stream-format=byte-stream"CAPS_FRAGMENT, FALSE},
  {GST_DROID_CODEC_DECODER, "video/x-h263", "video/3gpp", NULL,
      NULL, "video/x-h263"CAPS_FRAGMENT, FALSE},

  /* encoders */
  {GST_DROID_CODEC_ENCODER, "video/mpeg", "video/mp4v-es", is_mpeg4v,
      NULL, "video/mpeg, mpegversion=4, systemstream=false"CAPS_FRAGMENT, FALSE},
  {GST_DROID_CODEC_ENCODER, "video/x-h264", "video/avc",
        is_h264_enc, h264_compliment,
      "video/x-h264, alignment=au, stream-format=byte-stream"CAPS_FRAGMENT, TRUE},
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
