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
#include "plugin.h"
#include <string.h>

#define gst_droidenc_parent_class parent_class
G_DEFINE_TYPE (GstDroidEnc, gst_droidenc, GST_TYPE_VIDEO_ENCODER);

GST_DEBUG_CATEGORY_EXTERN (gst_droid_enc_debug);
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

#define GST_DROID_ENC_TARGET_BITRATE_DEFAULT 192000

static void
gst_droidenc_signal_eos (void *data)
{
  GstDroidEnc * enc = (GstDroidEnc *) data;

  GST_DEBUG_OBJECT (enc, "codec signaled EOS");

  // TODO:
}

static void
gst_droidenc_error (void *data, int err)
{
  GstDroidEnc * enc = (GstDroidEnc *) data;

  GST_DEBUG_OBJECT (enc, "codec error");

  GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, NULL, ("error 0x%x from android codec", -err));

  // TODO:
}

static void
gst_droidenc_data_available(void *data, DroidMediaCodecData *encoded)
{
  GstVideoCodecFrame *frame;
  GstDroidEnc * enc = (GstDroidEnc *) data;

  GST_DEBUG_OBJECT (enc, "data available");

  if (encoded->codec_config) {
    GstBuffer *codec_data =
      gst_buffer_new_allocate (NULL, encoded->data.size, NULL);
    GST_INFO_OBJECT (enc, "received codec_data");
    gst_buffer_fill (codec_data, 0, encoded->data.data, encoded->data.size);

    GST_BUFFER_OFFSET (codec_data) = 0;
    GST_BUFFER_OFFSET_END (codec_data) = 0;
    GST_BUFFER_PTS (codec_data) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DTS (codec_data) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (codec_data) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_FLAG_SET (codec_data, GST_BUFFER_FLAG_HEADER);

    if (enc->codec_type->in_stream_headers) {
      GstVideoEncoder *encoder = GST_VIDEO_ENCODER (enc);
      GList *headers = NULL;
      headers = g_list_append (headers, codec_data);
      gst_video_encoder_set_headers (encoder, headers);
    } else {
      gst_buffer_replace (&enc->out_state->codec_data, codec_data);
    }

    return;
  }

  frame = gst_video_encoder_get_oldest_frame (GST_VIDEO_ENCODER (enc));
  if (!frame) {
    // TODO:
    GST_WARNING_OBJECT (enc, "buffer without frame");
    return;
  }
  // TODO:
  //  frame->pts = data->ts;

  frame->output_buffer = gst_video_encoder_allocate_output_buffer (GST_VIDEO_ENCODER (enc),
								   encoded->data.size);
  gst_buffer_fill (frame->output_buffer, 0, encoded->data.data, encoded->data.size);

  GST_BUFFER_TIMESTAMP (frame->output_buffer) = encoded->ts;

  if (encoded->sync) {
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT(frame);
  }

  gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (enc), frame);

#if 0
  GstDroidDec *dec = (GstDroidDec *) user;
  DroidMediaBuffer *buffer;
  DroidMediaBufferCallbacks cb;
  GstMemory *mem;
  guint width, height;

  

  GST_DEBUG_OBJECT (dec, "frame available");

  mem = gst_wrapped_memory_allocator_memory_new (dec->allocator);

  cb.ref = (void (*)(void *))gst_memory_ref;
  cb.unref = (void (*)(void *))gst_memory_unref;
  cb.data = mem;

  buffer = droid_media_codec_acquire_buffer(dec->codec, &cb);
  if (!buffer) {
    GST_ERROR_OBJECT (dec, "failed to acquire buffer from droidmedia");
    gst_memory_unref (mem);
    return;
  }

  gst_wrapped_memory_allocator_memory_set_data (mem, buffer,
	(GFunc)gst_droiddec_release_frame, NULL);

  GstBuffer *buff = gst_buffer_new ();

  gst_buffer_insert_memory (buff, 0, mem);

  width = droid_media_buffer_get_width(buffer);
  height = droid_media_buffer_get_height(buffer);

  gst_buffer_add_video_meta (buff, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_FORMAT_ENCODED,
			     width, height);

  frame = gst_video_decoder_get_oldest_frame (GST_VIDEO_DECODER (dec));

  if (!frame) {
    // TODO:
    GST_WARNING_OBJECT (dec, "buffer without frame");
    gst_buffer_unref (buff);
    return;
  }

  frame->output_buffer = buff;

  /* We get the timestamp in ns already */
  frame->pts = droid_media_buffer_get_timestamp (buffer);

  gst_video_decoder_finish_frame (GST_VIDEO_DECODER (dec), frame);
#endif
}

static gboolean
gst_droidenc_do_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  gboolean ret;
  GstDroidEnc *enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "do handle frame");

  /* This can deadlock if droidmedia/stagefright input buffer queue is full thus we
   * cannot write the input buffer. We end up waiting for the write operation
   * which does not happen because stagefright needs us to provide
   * output buffers to be filled (which can not happen because _loop() tries
   * to call get_oldest_frame() which acquires the stream lock the base class
   * is holding before calling us
   */
  // TODO: Check that we are still in a sane state

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
  if (!gst_droid_codec_consume_frame (enc->codec, frame, frame->pts)) {
    ret = FALSE;
    goto out;
  }

  ret = TRUE;

#if 0


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
#endif

out:
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  GST_DEBUG_OBJECT (enc, "do handle frame done %d", ret);

  return ret;
}

#if 0
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
#endif

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

  if (enc->codec_type->compliment) {
    enc->codec_type->compliment (caps);
  }

  out = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (enc),
      caps, enc->in_state);

  return out;
}

#if 0
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
      if (enc->in_stream_headers) {
        GstVideoEncoder *encoder = GST_VIDEO_ENCODER (enc);
        GList *headers = NULL;
        headers = g_list_append (headers, codec_data);
        gst_video_encoder_set_headers (encoder, headers);
      } else {
        gst_buffer_replace (&enc->out_state->codec_data, codec_data);
        gst_buffer_unref (buffer);
      }

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

    if (buff->nFlags & OMX_BUFFERFLAG_SYNCFRAME) {
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    }

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
#endif

static void
gst_droidenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDroidEnc *enc = GST_DROIDENC (object);

  switch (prop_id) {
    case PROP_TARGET_BITRATE:
      enc->target_bitrate = g_value_get_int (value);
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
      g_value_set_int (value, enc->target_bitrate);
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

  //  gst_mini_object_unref (GST_MINI_OBJECT (enc->codec));
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
  //  gst_droidenc_stop_loop (encoder);
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  if (enc->in_state) {
    gst_video_codec_state_unref (enc->in_state);
    enc->in_state = NULL;
  }

  if (enc->out_state) {
    gst_video_codec_state_unref (enc->out_state);
    enc->out_state = NULL;
  }
#if 0
  if (enc->comp) {
    gst_droid_codec_stop_component (enc->comp);
    gst_droid_codec_destroy_component (enc->comp);
    enc->comp = NULL;
  }
#endif

  GST_DEBUG_OBJECT (enc, "stopped");

  return TRUE;
}

static gboolean
gst_droidenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  DroidMediaCodecEncoderMetaData md;
  GstDroidEnc *enc = GST_DROIDENC (encoder);
  GstCaps *caps = NULL;

  GST_DEBUG_OBJECT (enc, "set format %" GST_PTR_FORMAT, state->caps);

  if (enc->codec) {
    /* TODO */
    GST_ERROR_OBJECT (enc, "cannot renegotiate");
    return FALSE;
  }

  enc->first_frame_sent = FALSE;

  enc->in_state = gst_video_codec_state_ref (state);

  caps = gst_pad_peer_query_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder), NULL);

  GST_DEBUG_OBJECT (enc, "peer caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_truncate (caps);

  /* try to get our codec */
  enc->codec_type = gst_droid_codec_get_from_caps (caps, GST_DROID_CODEC_ENCODER);
  if (!enc->codec_type) {
    GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, (NULL),
        ("Unknown codec type for caps %" GST_PTR_FORMAT, state->caps));

    /* TODO: in_state */
    gst_caps_unref (caps);
    return FALSE;
  }

  md.parent.type = enc->codec_type->droid;

  enc->out_state =
      gst_droidenc_configure_state (encoder, &state->info, caps);

#if 0
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

#endif

  md.parent.width = state->info.width;
  md.parent.height = state->info.height;
  md.parent.fps = state->info.fps_n / state->info.fps_d; // TODO: bad
  md.parent.flags = DROID_MEDIA_CODEC_HW_ONLY;
  md.bitrate = enc->target_bitrate;
  md.stride = state->info.width;
  md.slice_height = state->info.height;

  /* TODO: get this from caps */
  md.meta_data = true;

  enc->codec = droid_media_codec_create_encoder (&md);

  if (!enc->codec) {
    GST_ELEMENT_ERROR(enc, LIBRARY, SETTINGS, NULL, ("Failed to create encoder"));
    return FALSE;
  }

  {
    DroidMediaCodecCallbacks cb;
    cb.signal_eos = gst_droidenc_signal_eos;
    cb.error = gst_droidenc_error;
    droid_media_codec_set_callbacks (enc->codec, &cb, enc);
  }

  {
    DroidMediaCodecDataCallbacks cb;
    cb.data_available = gst_droidenc_data_available;
    droid_media_codec_set_data_callbacks (enc->codec, &cb, enc);
  }

  if (!droid_media_codec_start (enc->codec)) {
    // TODO: error
    droid_media_codec_destroy (enc->codec);
    enc->codec = NULL;
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_droidenc_finish (GstVideoEncoder * encoder)
{
  GstDroidEnc *enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "finish");

  //  gst_droidenc_stop_loop (encoder);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_droidenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstDroidEnc *enc = GST_DROIDENC (encoder);
  GstFlowReturn ret = GST_FLOW_ERROR;

  GST_DEBUG_OBJECT (enc, "handle frame");

  if (!enc->codec) {
    GST_ERROR_OBJECT (enc, "component not initialized");
    goto out;
  }

#if 0

  if (gst_droid_codec_has_error (enc->comp)) {
    GST_ERROR_OBJECT (enc, "not handling frame while omx is in error state");
    goto out;
  }

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    OMX_CONFIG_INTRAREFRESHVOPTYPE config;
    OMX_ERRORTYPE err;

    GST_OMX_INIT_STRUCT (&config);
    config.nPortIndex = enc->comp->in_port->def.nPortIndex;
    config.IntraRefreshVOP = OMX_TRUE;

    GST_DEBUG_OBJECT (enc, "forcing a keyframe");

    err = gst_droid_codec_set_config (enc->comp,
        OMX_IndexConfigVideoIntraVOPRefresh, &config);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (enc, "got error %s (0x%08x) forcing a keyframe",
          gst_omx_error_to_string (err), err);
    }
  }

  if (!gst_droid_codec_is_running (enc->comp)) {
    ret = GST_FLOW_FLUSHING;
    goto out;
  }


#endif

  if (gst_droidenc_do_handle_frame (encoder, frame)) {
    ret = GST_FLOW_OK;
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

#if 0
  if (!enc->comp) {
    GST_DEBUG_OBJECT (enc, "no component to flush");
    return TRUE;
  }

  gst_droidenc_stop_loop (encoder);

  /* now flush our component */
  if (!gst_droid_codec_flush (enc->comp, TRUE)) {
    return FALSE;
  }
#endif

  GST_DEBUG_OBJECT (enc, "Flushed");

  return TRUE;
}

static void
gst_droidenc_init (GstDroidEnc * enc)
{
  enc->codec = NULL;
  enc->codec_type = NULL;
  enc->in_state = NULL;
  enc->out_state = NULL;
  enc->target_bitrate = GST_DROID_ENC_TARGET_BITRATE_DEFAULT;
}

static GstCaps *
gst_droidenc_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  GstDroidEnc *enc;
  GstCaps *caps;
  GstCaps *ret;

  enc = GST_DROIDENC (encoder);

  GST_DEBUG_OBJECT (enc, "getcaps with filter %" GST_PTR_FORMAT, filter);

#if 0
  /*
   * TODO: Seems _proxy_getcaps() is not working for us. It might be related to the feature we use
   * If it's the case then file a bug upstream, try to fix it and then enable this.
   */
  if (enc->out_state && enc->out_state->caps) {
    caps = gst_caps_copy (enc->out_state->caps);
  } else {
    caps = gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SINK_PAD (encoder));
  }

  GST_DEBUG_OBJECT (enc, "our caps %" GST_PTR_FORMAT, caps);

  ret = gst_video_encoder_proxy_getcaps (encoder, caps, filter);

  GST_DEBUG_OBJECT (enc, "returning %" GST_PTR_FORMAT, ret);

  gst_caps_unref (caps);
#endif

  if (enc->out_state && enc->out_state->caps) {
    caps = gst_caps_copy (enc->out_state->caps);
  } else {
    caps = gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SINK_PAD (encoder));
  }

  GST_DEBUG_OBJECT (enc, "our caps %" GST_PTR_FORMAT, caps);

  if (caps && filter) {
    ret = gst_caps_intersect_full (caps, filter, GST_CAPS_INTERSECT_FIRST);
  } else if (caps) {
    ret = gst_caps_ref (caps);
  } else {
    ret = NULL;
  }

  if (caps) {
    gst_caps_unref (caps);
  }

  GST_DEBUG_OBJECT (enc, "returning caps %" GST_PTR_FORMAT, ret);

  return ret;
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

  caps = gst_droid_codec_get_all_caps (GST_DROID_CODEC_ENCODER);

  tpl = gst_pad_template_new (GST_VIDEO_ENCODER_SRC_NAME,
      GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (gstelement_class, tpl);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droidenc_sink_template_factory));

  gobject_class->finalize = gst_droidenc_finalize;
  gobject_class->set_property = gst_droidenc_set_property;
  gobject_class->get_property = gst_droidenc_get_property;

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
      g_param_spec_int ("target-bitrate", "Target Bitrate",
          "Target bitrate", 0, G_MAXINT,
          GST_DROID_ENC_TARGET_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
