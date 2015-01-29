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
