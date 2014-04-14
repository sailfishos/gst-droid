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

#include "gstdroidenc.h"
#include "gst/memory/gstwrappedmemory.h"
#include "gst/memory/gstgralloc.h"
#include "gstdroidcodectype.h"
#include "plugin.h"
#include <string.h>

#define gst_droidenc_parent_class parent_class
G_DEFINE_TYPE (GstDroidEnc, gst_droidenc, GST_TYPE_VIDEO_ENCODER);

GST_DEBUG_CATEGORY (gst_droid_enc_debug);
#define GST_CAT_DEFAULT gst_droid_enc_debug

static GstStaticPadTemplate gst_droidenc_sink_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_ENCODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_VIDEO_META_DATA, "{ENCODED, YV12}")));

enum
{
  PROP_0,
  PROP_TARGET_BITRATE,
};

static gboolean
gst_droidenc_do_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  gboolean ret;
  GstDroidEnc *enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "do handle frame");

  /* This can deadlock if omx does not provide an input buffer and we end up
   * waiting for a buffer which does not happen because omx needs us to provide
   * output buffers to be filled (which can not happen because _loop() tries
   * to call get_oldest_frame() which acquires the stream lock the base class
   * is holding before calling us */

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
  if (!gst_droid_codec_consume_frame (enc->comp, frame)) {
    ret = FALSE;
    goto out;
  }

  ret = TRUE;

out:
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  GST_DEBUG_OBJECT (enc, "do handle frame done %d", ret);

  return ret;
}

static void
gst_droidenc_stop_loop (GstVideoEncoder * encoder)
{
  GstDroidEnc *enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "stop loop");

  if (!enc->comp) {
    /* nothing to do here */
    return;
  }

  gst_droid_codec_set_running (enc->comp, FALSE);

  /* That should be enough for now as we can not deactivate our buffer pools
   * otherwise we end up freeing the buffers before deactivating our omx ports
   */

  /* Just informing the task that we are finishing */
  g_mutex_lock (&enc->comp->full_lock);
  /* if the queue is empty then we _prepend_ a NULL buffer
   * which should not be a problem because the loop is running
   * and it will pop it. If it's not then we will clean it up later. */
  if (enc->comp->full->length == 0) {
    g_queue_push_head (enc->comp->full, NULL);
  }

  g_cond_signal (&enc->comp->full_cond);
  g_mutex_unlock (&enc->comp->full_lock);

  /* We need to release the stream lock to prevent deadlocks when the _loop ()
   * function tries to call _finish_frame ()
   */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
  GST_PAD_STREAM_LOCK (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  GST_PAD_STREAM_UNLOCK (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  if (!gst_pad_stop_task (GST_VIDEO_ENCODER_SRC_PAD (encoder))) {
    GST_WARNING_OBJECT (enc, "failed to stop src pad task");
  }

  GST_DEBUG_OBJECT (enc, "stopped loop");
}

static GstVideoCodecState *
gst_droidenc_configure_state (GstVideoEncoder * encoder,
    GstVideoInfo * info, GstCaps * caps)
{
  GstVideoCodecState *out = NULL;
  GstDroidEnc *enc = GST_DROIDENC (encoder);
  GST_DEBUG_OBJECT (enc, "configure state: width: %d, height: %d",
      info->width, info->height);

  GST_DEBUG_OBJECT (enc, "peer caps %" GST_PTR_FORMAT, caps);

  /* we care about width, height and framerate */
  gst_caps_set_simple (caps, "width", G_TYPE_INT, info->width,
      "height", G_TYPE_INT, info->height,
      "framerate", GST_TYPE_FRACTION, info->fps_n, info->fps_d, NULL);

  caps = gst_caps_fixate (caps);

  out = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (enc),
      caps, enc->in_state);

  return out;
}

static void
gst_droidenc_loop (GstDroidEnc * enc)
{
  OMX_BUFFERHEADERTYPE *buff;
  GstBuffer *buffer;
  GstVideoCodecFrame *frame;
  GstMapInfo map = GST_MAP_INFO_INIT;

  while (gst_droid_codec_is_running (enc->comp)) {
    if (gst_droid_codec_has_error (enc->comp)) {
      return;
    }

    if (!gst_droid_codec_return_output_buffers (enc->comp)) {
      GST_WARNING_OBJECT (enc,
          "failed to return output buffers to the encoder");
    }

    GST_DEBUG_OBJECT (enc, "trying to get a buffer");
    g_mutex_lock (&enc->comp->full_lock);
    buff = g_queue_pop_head (enc->comp->full);

    if (!buff) {
      if (!gst_droid_codec_is_running (enc->comp)) {
        /* this is a signal that we should quit */
        GST_DEBUG_OBJECT (enc, "got no buffer");
        g_mutex_unlock (&enc->comp->full_lock);
        continue;
      }

      g_cond_wait (&enc->comp->full_cond, &enc->comp->full_lock);
      buff = g_queue_pop_head (enc->comp->full);
    }

    g_mutex_unlock (&enc->comp->full_lock);
    GST_DEBUG_OBJECT (enc, "got buffer %p", buff);

    if (!buff) {
      GST_DEBUG_OBJECT (enc, "got no buffer");
      /* This can only happen if we are not running
       * yet we will not exit because we should detect
       * that upon looping */
      continue;
    }

    buffer = gst_omx_buffer_get_buffer (enc->comp, buff);
    if (!buffer) {
      GST_ERROR_OBJECT (enc, "can not get buffer associated with omx buffer %p",
          buff);
      continue;
    }

    /* is it codec config? */
    if (buff->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
      GstBuffer *codec_data =
          gst_buffer_new_allocate (NULL, buff->nFilledLen, NULL);
      GST_INFO_OBJECT (enc, "received codec_data");
      gst_buffer_fill (codec_data, 0, buff->pBuffer + buff->nOffset,
          buff->nFilledLen);
      gst_buffer_replace (&enc->out_state->codec_data, codec_data);
      gst_buffer_unref (buffer);

      continue;
    }

    if (!enc->first_frame_sent) {
      enc->first_frame_sent = TRUE;

      if (!gst_video_encoder_negotiate (GST_VIDEO_ENCODER (enc))) {
        GST_ELEMENT_ERROR (enc, STREAM, FORMAT, (NULL),
            ("failed to negotiate output format"));

        gst_buffer_unref (buffer);
        continue;
      }

    }

    /* Now we can proceed. */
    frame = gst_video_encoder_get_oldest_frame (GST_VIDEO_ENCODER (enc));
    if (!frame) {
      gst_buffer_unref (buffer);
      GST_ERROR_OBJECT (enc, "can not find a video frame");
      continue;
    }

    if (!buff->nFilledLen) {
      GST_WARNING_OBJECT (enc, "received empty buffer");
      gst_video_codec_frame_unref (frame);      /* we have an extra ref from _get_oldest_frame */
      gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (enc), frame);
      gst_buffer_unref (buffer);
      continue;
    }

    frame->output_buffer =
        gst_video_encoder_allocate_output_buffer (GST_VIDEO_ENCODER (enc),
        buff->nFilledLen);

    gst_buffer_map (frame->output_buffer, &map, GST_MAP_WRITE);
    memcpy (map.data, buff->pBuffer + buff->nOffset, buff->nFilledLen);
    gst_buffer_unmap (frame->output_buffer, &map);

    GST_DEBUG_OBJECT (enc, "finishing frame %p", frame);

    gst_droid_codec_timestamp (frame->output_buffer, buff);

    gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (enc), frame);
    gst_video_codec_frame_unref (frame);
    gst_buffer_unref (buffer);
  }

  if (!gst_droid_codec_is_running (enc->comp)) {
    GST_DEBUG_OBJECT (enc, "stopping task");

    if (!gst_pad_pause_task (GST_VIDEO_ENCODER_SRC_PAD (GST_VIDEO_ENCODER
                (enc)))) {
      GST_WARNING_OBJECT (enc, "failed to pause src pad task");
    }

    return;
  }
}

static void
gst_droidenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDroidEnc *enc = GST_DROIDENC (object);

  switch (prop_id) {
    case PROP_TARGET_BITRATE:
      enc->target_bitrate = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_droidenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDroidEnc *enc = GST_DROIDENC (object);

  switch (prop_id) {
    case PROP_TARGET_BITRATE:
      g_value_set_uint (value, enc->target_bitrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_droidenc_finalize (GObject * object)
{
  GstDroidEnc *enc = GST_DROIDENC (object);

  GST_DEBUG_OBJECT (enc, "finalize");

  gst_mini_object_unref (GST_MINI_OBJECT (enc->codec));
  enc->codec = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_droidenc_open (GstVideoEncoder * encoder)
{
  GstDroidEnc *enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "open");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droidenc_close (GstVideoEncoder * encoder)
{
  GstDroidEnc *enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "close");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droidenc_start (GstVideoEncoder * encoder)
{
  GstDroidEnc *enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "start");

  return TRUE;
}

static gboolean
gst_droidenc_stop (GstVideoEncoder * encoder)
{
  GstDroidEnc *enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "stop");

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
  gst_droidenc_stop_loop (encoder);
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  if (enc->in_state) {
    gst_video_codec_state_unref (enc->in_state);
    enc->in_state = NULL;
  }

  if (enc->out_state) {
    gst_video_codec_state_unref (enc->out_state);
    enc->out_state = NULL;
  }

  if (enc->comp) {
    gst_droid_codec_stop_component (enc->comp);
    gst_droid_codec_destroy_component (enc->comp);
    enc->comp = NULL;
  }

  GST_DEBUG_OBJECT (enc, "stopped");

  return TRUE;
}

static gboolean
gst_droidenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  const gchar *type;
  GstDroidEnc *enc = GST_DROIDENC (encoder);
  GstCaps *caps = NULL;

  GST_DEBUG_OBJECT (enc, "set format %" GST_PTR_FORMAT, state->caps);

  if (enc->comp) {
    GST_ERROR_OBJECT (enc, "cannot renegotiate");
    return FALSE;
  }

  enc->first_frame_sent = FALSE;

  enc->in_state = gst_video_codec_state_ref (state);

  caps = gst_pad_peer_query_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder), NULL);

  GST_DEBUG_OBJECT (enc, "peer caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_truncate (caps);

  /* try to get our codec */
  type = gst_droid_codec_type_from_caps (caps, GST_DROID_CODEC_ENCODER);

  if (!type) {
    GST_DEBUG_OBJECT (enc, "failed to get any encoder");
    gst_caps_unref (caps);
    return FALSE;
  }

  enc->comp =
      gst_droid_codec_get_component (enc->codec, type, GST_ELEMENT (enc));
  if (!enc->comp) {
    GST_ERROR_OBJECT (enc, "failed to get component");
    gst_caps_unref (caps);
    return FALSE;
  }

  /* configure codec */
  if (!gst_droid_codec_configure_component (enc->comp, &state->info)) {
    gst_caps_unref (caps);
    return FALSE;
  }

  if (!gst_droid_codec_apply_encoding_params (enc->comp, &state->info, caps,
          enc->target_bitrate)) {
    gst_caps_unref (caps);
    return FALSE;
  }

  enc->out_state = gst_droidenc_configure_state (encoder, &state->info, caps);

  /* now start */
  if (!gst_droid_codec_start_component (enc->comp, enc->in_state->caps,
          enc->out_state->caps)) {
    return FALSE;
  }

  if (!gst_pad_start_task (GST_VIDEO_ENCODER_SRC_PAD (encoder),
          (GstTaskFunction) gst_droidenc_loop, gst_object_ref (enc),
          gst_object_unref)) {
    GST_ERROR_OBJECT (enc, "failed to start src task");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_droidenc_finish (GstVideoEncoder * encoder)
{
  GstDroidEnc *enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "finish");

  gst_droidenc_stop_loop (encoder);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_droidenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstDroidEnc *enc = GST_DROIDENC (encoder);
  GstFlowReturn ret = GST_FLOW_ERROR;

  GST_DEBUG_OBJECT (enc, "handle frame");

  if (gst_droid_codec_has_error (enc->comp)) {
    GST_ERROR_OBJECT (enc, "not handling frame while omx is in error state");
    goto out;
  }

  if (gst_droidenc_do_handle_frame (encoder, frame)) {
    ret = GST_FLOW_OK;
    goto out;
  }

  if (!gst_droid_codec_is_running (enc->comp)) {
    ret = GST_FLOW_FLUSHING;
    goto out;
  }

out:
  /* don't leak the frame */
  if (ret != GST_FLOW_OK) {
    gst_video_encoder_finish_frame (encoder, frame);
  }

  return ret;
}

static gboolean
gst_droidenc_flush (GstVideoEncoder * encoder)
{
  GstDroidEnc *enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "flush");

  gst_droidenc_stop_loop (encoder);

  /* now flush our component */
  if (!gst_droid_codec_flush (enc->comp, TRUE)) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (enc, "Flushed");

  return TRUE;
}

static void
gst_droidenc_init (GstDroidEnc * enc)
{
  enc->codec = gst_droid_codec_get ();
  enc->comp = NULL;
  enc->in_state = NULL;
  enc->out_state = NULL;
  enc->target_bitrate = GST_DROID_ENC_TARGET_BITRATE_DEFAULT;
}

static GstStateChangeReturn
gst_droidenc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDroidEnc *enc;
  GstVideoEncoder *encoder;

  encoder = GST_VIDEO_ENCODER (element);
  enc = GST_DROIDENC (element);

  GST_DEBUG_OBJECT (enc, "change state from %s to %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY) {
    GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
    gst_droidenc_stop_loop (encoder);
    GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static GstCaps *
gst_droidenc_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  GstDroidEnc *enc;
  GstCaps *caps;
  //  GstCaps *ret;

  enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "getcaps with filter %" GST_PTR_FORMAT, filter);

  // TODO: if we have caps then report them.
  caps = gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SINK_PAD (encoder));
  GST_DEBUG_OBJECT (enc, "our caps %" GST_PTR_FORMAT, caps);

#if 0
  ret = gst_video_encoder_proxy_getcaps (encoder, caps, filter);

  GST_DEBUG_OBJECT (enc, "returning %" GST_PTR_FORMAT, ret);

  gst_caps_unref (caps);

  return ret;
#endif
  // TODO:
  return caps;
}

static void
gst_droidenc_class_init (GstDroidEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideoEncoderClass *gstvideoencoder_class;
  GstCaps *caps;
  GstPadTemplate *tpl;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstvideoencoder_class = (GstVideoEncoderClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Video encoder", "Encoder/Video/Device",
      "Android HAL encoder", "Mohammed Sameer <msameer@foolab.org>");

  caps = gst_droid_codec_type_all_caps (GST_DROID_CODEC_ENCODER);

  tpl = gst_pad_template_new (GST_VIDEO_ENCODER_SRC_NAME,
      GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (gstelement_class, tpl);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droidenc_sink_template_factory));

  gobject_class->finalize = gst_droidenc_finalize;
  gobject_class->set_property = gst_droidenc_set_property;
  gobject_class->get_property = gst_droidenc_get_property;
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_droidenc_change_state);
  gstvideoencoder_class->open = GST_DEBUG_FUNCPTR (gst_droidenc_open);
  gstvideoencoder_class->close = GST_DEBUG_FUNCPTR (gst_droidenc_close);
  gstvideoencoder_class->start = GST_DEBUG_FUNCPTR (gst_droidenc_start);
  gstvideoencoder_class->stop = GST_DEBUG_FUNCPTR (gst_droidenc_stop);
  gstvideoencoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_droidenc_set_format);
  gstvideoencoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_droidenc_getcaps);
  gstvideoencoder_class->finish = GST_DEBUG_FUNCPTR (gst_droidenc_finish);
  gstvideoencoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_droidenc_handle_frame);
  gstvideoencoder_class->flush = GST_DEBUG_FUNCPTR (gst_droidenc_flush);

  g_object_class_install_property (gobject_class, PROP_TARGET_BITRATE,
      g_param_spec_uint ("target-bitrate", "Target Bitrate",
          "Target bitrate (0xffffffff=component default)", 0, G_MAXUINT,
          GST_DROID_ENC_TARGET_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
