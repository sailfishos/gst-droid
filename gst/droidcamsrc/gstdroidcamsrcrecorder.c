/*
 * gst-droid
 *
 * Copyright (C) 2016 Jolla LTD.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "gstdroidcamsrcrecorder.h"
#include "gstdroidcamsrc.h"
#include <gst/droid/gstdroidcodec.h>

#define GST_DROIDCAMSRC_RECORDER_TARGET_BITRATE_DEFAULT 192000

static void gst_droidcamsrc_recorder_data_available (void *data,
    DroidMediaCodecData * encoded);

GstDroidCamSrcRecorder *
gst_droidcamsrc_recorder_create (GstDroidCamSrcPad * vidsrc)
{
  GstDroidCamSrcRecorder *recorder = g_new0 (GstDroidCamSrcRecorder, 1);

  recorder->vidsrc = vidsrc;
  recorder->md.bitrate = GST_DROIDCAMSRC_RECORDER_TARGET_BITRATE_DEFAULT;
  recorder->md.meta_data = true;
  recorder->md.parent.flags = DROID_MEDIA_CODEC_HW_ONLY;

  return recorder;
}

void
gst_droidcamsrc_recorder_destroy (GstDroidCamSrcRecorder * recorder)
{
  if (recorder->codec) {
    gst_droid_codec_unref (recorder->codec);
  }

  g_free (recorder);
}

gboolean
gst_droidcamsrc_recorder_init (GstDroidCamSrcRecorder * recorder,
    DroidMediaCamera * cam, gint32 target_bitrate)
{
  if (!recorder->codec) {
    return FALSE;
  }

  if (recorder->recorder) {
    droid_media_recorder_destroy (recorder->recorder);
  }

  recorder->md.bitrate = target_bitrate;

  /* set the color format */
  recorder->md.color_format = droid_media_camera_get_video_color_format (cam);

  recorder->recorder = droid_media_recorder_create (cam, &recorder->md);

  if (!recorder->recorder) {
    return FALSE;
  }

  DroidMediaCodecDataCallbacks cb;
  cb.data_available = gst_droidcamsrc_recorder_data_available;

  droid_media_recorder_set_data_callbacks (recorder->recorder, &cb, recorder);

  return TRUE;
}

void
gst_droidcamsrc_recorder_update_vid (GstDroidCamSrcRecorder * recorder,
    GstVideoInfo * info, GstCaps * caps)
{
  recorder->codec =
      gst_droid_codec_new_from_caps (caps, GST_DROID_CODEC_ENCODER_VIDEO);
  recorder->md.parent.width = info->width;
  recorder->md.parent.height = info->height;
  recorder->md.stride = recorder->md.parent.width;
  recorder->md.slice_height = recorder->md.parent.height;
  recorder->md.parent.fps = info->fps_n / info->fps_d;

  if (recorder->codec) {
    recorder->md.parent.type = gst_droid_codec_get_droid_type (recorder->codec);
  }
}

gboolean
gst_droidcamsrc_recorder_start (GstDroidCamSrcRecorder * recorder)
{
  return droid_media_recorder_start (recorder->recorder);
}

void
gst_droidcamsrc_recorder_stop (GstDroidCamSrcRecorder * recorder)
{
  droid_media_recorder_stop (recorder->recorder);
}

static void
gst_droidcamsrc_recorder_data_available (void *data,
    DroidMediaCodecData * encoded)
{
  GstDroidCamSrcRecorder *recorder = (GstDroidCamSrcRecorder *) data;
  GstDroidCamSrc *src =
      GST_DROIDCAMSRC (GST_PAD_PARENT (recorder->vidsrc->pad));
  GstBuffer *buffer = NULL;

  if (encoded->codec_config) {
    GstBuffer *codec_data = NULL;
    GstCaps *caps = NULL;
    GstCaps *current;
    gboolean ret;

    codec_data =
        gst_droid_codec_create_encoder_codec_data (recorder->codec,
        &encoded->data);

    if (!codec_data) {
      GST_ELEMENT_ERROR (src, STREAM, FORMAT, (NULL),
          ("Failed to construct codec_data. Expect corrupted stream"));
      return;
    }

    current = gst_pad_get_current_caps (recorder->vidsrc->pad);
    caps = gst_caps_copy (current);
    gst_caps_unref (current);
    current = NULL;

    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
    ret = gst_pad_set_caps (recorder->vidsrc->pad, caps);
    gst_caps_unref (caps);

    if (!ret) {
      GST_ELEMENT_ERROR (src, STREAM, FORMAT, (NULL),
          ("Failed to set video caps"));
    }

    return;
  }

  buffer =
      gst_droid_codec_prepare_encoded_data (recorder->codec, &encoded->data);
  if (!buffer) {
    GST_ELEMENT_ERROR (src, LIBRARY, ENCODE, (NULL),
        ("failed to process encoded data"));
    return;
  }

  GST_BUFFER_PTS (buffer) = encoded->ts;
  GST_BUFFER_DTS (buffer) = encoded->decoding_ts;

  if (encoded->sync) {
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  } else {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  gst_droidcamsrc_dev_queue_video_buffer (src->dev, buffer);
}
