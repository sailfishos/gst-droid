/*
 * gst-droid
 *
 * Copyright (C) 2014-2015 Mohammed Sameer <msameer@foolab.org>
 * Copyright (C) 2015 Jolla LTD.
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

#include "gstdroidvenc.h"
#include "gst/droid/gstwrappedmemory.h"
#include "gst/droid/gstdroidquery.h"
#include "plugin.h"
#include <string.h>

#define gst_droidvenc_parent_class parent_class
G_DEFINE_TYPE (GstDroidVEnc, gst_droidvenc, GST_TYPE_VIDEO_ENCODER);

GST_DEBUG_CATEGORY_EXTERN (gst_droid_venc_debug);
#define GST_CAT_DEFAULT gst_droid_venc_debug

#define GST_DROIDVENC_EOS_TIMEOUT_SEC          2

static GstStaticPadTemplate gst_droidvenc_sink_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_ENCODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_VIDEO_META_DATA, "{YV12}")));

enum
{
  PROP_0,
  PROP_TARGET_BITRATE,
};

#define GST_DROID_ENC_TARGET_BITRATE_DEFAULT 192000

typedef struct
{
  GstMapInfo info;
  GstVideoCodecFrame *frame;
} GstDroidVEncFrameReleaseData;

static GstVideoCodecState *gst_droidvenc_configure_state (GstDroidVEnc * enc,
    GstCaps * caps);
static void gst_droidvenc_signal_eos (void *data);
static void gst_droidvenc_error (void *data, int err);
static void
gst_droidvenc_data_available (void *data, DroidMediaCodecData * encoded);
static void gst_droidvenc_release_input_frame (void *data);

static void
gst_droidvenc_release_input_frame (void *data)
{
  GstDroidVEncFrameReleaseData *release_data =
      (GstDroidVEncFrameReleaseData *) data;

  gst_buffer_unmap (release_data->frame->input_buffer, &release_data->info);

  /* We need to release the input buffer */
  gst_buffer_unref (release_data->frame->input_buffer);
  release_data->frame->input_buffer = NULL;

  gst_video_codec_frame_unref (release_data->frame);

  g_slice_free (GstDroidVEncFrameReleaseData, release_data);
}

static gboolean
gst_droidvenc_negotiate_src_caps (GstDroidVEnc * enc)
{
  GstCaps *caps;

  GST_DEBUG_OBJECT (enc, "negotiate src caps");

  caps =
      gst_pad_peer_query_caps (GST_VIDEO_ENCODER_SRC_PAD (GST_VIDEO_ENCODER
          (enc)), NULL);

  GST_LOG_OBJECT (enc, "peer caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_truncate (caps);

  enc->codec_type =
      gst_droid_codec_new_from_caps (caps, GST_DROID_CODEC_ENCODER_VIDEO);
  if (!enc->codec_type) {
    GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, (NULL),
        ("Unknown codec type for caps %" GST_PTR_FORMAT, caps));

    gst_caps_unref (caps);
    goto error;
  }

  /* ownership of caps is transferred */
  enc->out_state = gst_droidvenc_configure_state (enc, caps);

  return TRUE;

error:
  return FALSE;
}

static gboolean
gst_droidvenc_create_codec (GstDroidVEnc * enc)
{
  DroidMediaCodecEncoderMetaData md;
  GstQuery *query;

  const gchar *droid = gst_droid_codec_get_droid_type (enc->codec_type);

  GST_INFO_OBJECT (enc,
      "create codec of type: %s resolution: %dx%d bitrate: %d", droid,
      enc->in_state->info.width, enc->in_state->info.height,
      enc->target_bitrate);

  memset (&md, 0x0, sizeof (md));

  md.parent.type = droid;
  md.parent.width = enc->in_state->info.width;
  md.parent.height = enc->in_state->info.height;
  md.parent.fps = enc->in_state->info.fps_n / enc->in_state->info.fps_d;        // TODO: bad
  md.parent.flags = DROID_MEDIA_CODEC_HW_ONLY;
  md.bitrate = enc->target_bitrate;
  md.stride = enc->in_state->info.width;
  md.slice_height = enc->in_state->info.height;

  /* TODO: get this from caps */
  md.meta_data = true;

  query = gst_droid_query_new_video_color_format ();
  if (!gst_pad_peer_query (GST_VIDEO_ENCODER_SINK_PAD (GST_VIDEO_ENCODER (enc)),
          query)) {
    gst_query_unref (query);
    GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
        ("Failed to query video color format"));
    return FALSE;
  }

  if (!gst_droid_query_parse_video_color_format (query, &md.color_format)) {
    gst_query_unref (query);
    GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
        ("Failed to get video color format"));
    return FALSE;
  }

  gst_query_unref (query);

  enc->codec = droid_media_codec_create_encoder (&md);

  if (!enc->codec) {
    GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
        ("Failed to create encoder"));
    return FALSE;
  }

  {
    DroidMediaCodecCallbacks cb;
    cb.signal_eos = gst_droidvenc_signal_eos;
    cb.error = gst_droidvenc_error;
    droid_media_codec_set_callbacks (enc->codec, &cb, enc);
  }

  {
    DroidMediaCodecDataCallbacks cb;
    cb.data_available = gst_droidvenc_data_available;
    droid_media_codec_set_data_callbacks (enc->codec, &cb, enc);
  }

  if (!droid_media_codec_start (enc->codec)) {
    GST_ELEMENT_ERROR (enc, LIBRARY, INIT, (NULL),
        ("Failed to start the encoder"));

    droid_media_codec_destroy (enc->codec);
    enc->codec = NULL;
    return FALSE;
  }

  return TRUE;
}

static void
gst_droidvenc_signal_eos (void *data)
{
  GstDroidVEnc *enc = (GstDroidVEnc *) data;

  GST_DEBUG_OBJECT (enc, "codec signaled EOS");

  g_mutex_lock (&enc->eos_lock);

  if (!enc->eos) {
    GST_WARNING_OBJECT (enc, "codec signaled EOS but we are not expecting it");
  }

  g_cond_signal (&enc->eos_cond);
  g_mutex_unlock (&enc->eos_lock);
}

static void
gst_droidvenc_error (void *data, int err)
{
  GstDroidVEnc *enc = (GstDroidVEnc *) data;

  GST_DEBUG_OBJECT (enc, "codec error");

  g_mutex_lock (&enc->eos_lock);

  if (enc->eos) {
    /* Gotta love Android. We will ignore errors if we are expecting EOS */
    g_cond_signal (&enc->eos_cond);
    goto out;
  }

  GST_VIDEO_ENCODER_STREAM_LOCK (enc);
  enc->downstream_flow_ret = GST_FLOW_ERROR;
  GST_VIDEO_ENCODER_STREAM_UNLOCK (enc);

  GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, NULL,
      ("error 0x%x from android codec", -err));

out:
  g_mutex_unlock (&enc->eos_lock);
}

static void
gst_droidvenc_data_available (void *data, DroidMediaCodecData * encoded)
{
  GstVideoCodecFrame *frame;
  GstFlowReturn flow_ret;
  GstDroidVEnc *enc = (GstDroidVEnc *) data;
  GstVideoEncoder *encoder = GST_VIDEO_ENCODER (enc);

  GST_DEBUG_OBJECT (enc, "data available");

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  if (encoded->codec_config) {
    GstBuffer *codec_data = NULL;

    GST_INFO_OBJECT (enc, "received codec_data");

    codec_data =
        gst_droid_codec_create_encoder_codec_data (enc->codec_type,
        &encoded->data);

    if (!codec_data) {
      enc->downstream_flow_ret = GST_FLOW_ERROR;

      GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

      GST_ELEMENT_ERROR (enc, STREAM, FORMAT, (NULL),
          ("Failed to construct codec_data. Expect corrupted stream"));

      return;
    }

    gst_buffer_replace (&enc->out_state->codec_data, codec_data);

    GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
    return;
  }

  frame = gst_video_encoder_get_oldest_frame (GST_VIDEO_ENCODER (enc));
  if (G_UNLIKELY (!frame)) {
    /* TODO: what should we do here? */
    GST_WARNING_OBJECT (enc, "buffer without frame");

    GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
    return;
  }

  frame->output_buffer =
      gst_droid_codec_prepare_encoded_data (enc->codec_type, &encoded->data);
  if (!frame->output_buffer) {
    GST_ELEMENT_ERROR (enc, LIBRARY, ENCODE, (NULL),
        ("failed to process encoded data"));
    gst_video_codec_frame_unref (frame);
    gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (enc), frame);
    enc->downstream_flow_ret = GST_FLOW_ERROR;
    GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
    return;
  }

  GST_BUFFER_PTS (frame->output_buffer) = encoded->ts;
  GST_BUFFER_DTS (frame->output_buffer) = encoded->decoding_ts;

  if (encoded->sync) {
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  }

  flow_ret = gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (enc), frame);
  /* release our ref */
  gst_video_codec_frame_unref (frame);

  if (flow_ret == GST_FLOW_OK || flow_ret == GST_FLOW_FLUSHING) {
    goto out;
  } else if (flow_ret == GST_FLOW_EOS) {
    GST_INFO_OBJECT (enc, "eos");
  } else if (flow_ret < GST_FLOW_OK) {
    GST_ELEMENT_ERROR (enc, STREAM, FAILED,
        ("Internal data stream error."), ("stream stopped, reason %s",
            gst_flow_get_name (flow_ret)));
  }

out:
  enc->downstream_flow_ret = flow_ret;
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
}

static GstVideoCodecState *
gst_droidvenc_configure_state (GstDroidVEnc * enc, GstCaps * caps)
{
  GstVideoCodecState *out = NULL;

  GST_DEBUG_OBJECT (enc, "configure state: width: %d, height: %d",
      enc->in_state->info.width, enc->in_state->info.height);

  GST_LOG_OBJECT (enc, "caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_fixate (caps);

  gst_droid_codec_complement_caps (enc->codec_type, caps);

  out = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (enc),
      caps, enc->in_state);

  return out;
}

static void
gst_droidvenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDroidVEnc *enc = GST_DROIDVENC (object);

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
gst_droidvenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDroidVEnc *enc = GST_DROIDVENC (object);

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
gst_droidvenc_finalize (GObject * object)
{
  GstDroidVEnc *enc = GST_DROIDVENC (object);

  GST_DEBUG_OBJECT (enc, "finalize");

  enc->codec = NULL;

  g_mutex_clear (&enc->eos_lock);
  g_cond_clear (&enc->eos_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_droidvenc_open (GstVideoEncoder * encoder)
{
  GstDroidVEnc *enc = GST_DROIDVENC (encoder);

  GST_DEBUG_OBJECT (enc, "open");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droidvenc_close (GstVideoEncoder * encoder)
{
  GstDroidVEnc *enc = GST_DROIDVENC (encoder);

  GST_DEBUG_OBJECT (enc, "close");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droidvenc_start (GstVideoEncoder * encoder)
{
  GstDroidVEnc *enc = GST_DROIDVENC (encoder);

  GST_DEBUG_OBJECT (enc, "start");

  enc->eos = FALSE;
  enc->downstream_flow_ret = GST_FLOW_OK;
  enc->dirty = TRUE;

  return TRUE;
}

static gboolean
gst_droidvenc_stop (GstVideoEncoder * encoder)
{
  GstDroidVEnc *enc = GST_DROIDVENC (encoder);

  GST_DEBUG_OBJECT (enc, "stop");

  if (enc->codec) {
    droid_media_codec_stop (enc->codec);
    droid_media_codec_destroy (enc->codec);
    enc->codec = NULL;
    enc->dirty = TRUE;
  }

  if (enc->in_state) {
    gst_video_codec_state_unref (enc->in_state);
    enc->in_state = NULL;
  }

  if (enc->out_state) {
    gst_video_codec_state_unref (enc->out_state);
    enc->out_state = NULL;
  }

  if (enc->codec_type) {
    gst_droid_codec_unref (enc->codec_type);
    enc->codec_type = NULL;
  }

  return TRUE;
}

static gboolean
gst_droidvenc_set_format (GstVideoEncoder * encoder, GstVideoCodecState * state)
{
  GstDroidVEnc *enc = GST_DROIDVENC (encoder);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (enc, "set format %" GST_PTR_FORMAT, state->caps);

  if (enc->codec) {
    GST_FIXME_OBJECT (enc, "What to do here?");
    GST_ERROR_OBJECT (enc, "codec already renegotiate");
    goto error;
  }

  enc->first_frame_sent = FALSE;

  enc->in_state = gst_video_codec_state_ref (state);

  if (!gst_droidvenc_negotiate_src_caps (enc)) {
    goto error;
  }

  /* handle_frame will create the codec */
  enc->dirty = TRUE;

  return TRUE;

error:
  if (enc->in_state) {
    gst_video_codec_state_unref (enc->in_state);
    enc->in_state = NULL;
  }

  if (enc->out_state) {
    gst_video_codec_state_unref (enc->out_state);
    enc->out_state = NULL;
  }

  return ret;
}

static GstFlowReturn
gst_droidvenc_finish (GstVideoEncoder * encoder)
{
  GstDroidVEnc *enc = GST_DROIDVENC (encoder);
  GTimeVal tv;

  GST_DEBUG_OBJECT (enc, "finish");

  g_mutex_lock (&enc->eos_lock);
  enc->eos = TRUE;

  if (enc->codec) {
    droid_media_codec_drain (enc->codec);
  } else {
    goto out;
  }

  g_get_current_time (&tv);
  g_time_val_add (&tv, G_USEC_PER_SEC * GST_DROIDVENC_EOS_TIMEOUT_SEC);

  /* release the lock to allow _frame_available () to do its job */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
  /* Now we wait for the codec to signal EOS. We cannot wait forever because sometimes
   * we never hear anything from the video encoders. */
  if (!g_cond_timed_wait (&enc->eos_cond, &enc->eos_lock, &tv)) {
    GST_WARNING_OBJECT (enc, "timeout waiting for eos");
  }

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

out:
  enc->eos = FALSE;

  if (enc->codec) {
    droid_media_codec_stop (enc->codec);
    droid_media_codec_destroy (enc->codec);
    enc->codec = NULL;
    enc->dirty = TRUE;
  }

  g_mutex_unlock (&enc->eos_lock);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_droidvenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstDroidVEnc *enc = GST_DROIDVENC (encoder);
  GstFlowReturn ret = GST_FLOW_ERROR;
  DroidMediaCodecData data;
  GstMapInfo info;
  DroidMediaBufferCallbacks cb;
  GstDroidVEncFrameReleaseData *release_data;

  GST_DEBUG_OBJECT (enc, "handle frame");

  if (enc->downstream_flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (enc, "not handling frame in error state: %s",
        gst_flow_get_name (enc->downstream_flow_ret));
    ret = enc->downstream_flow_ret;
    goto error;
  }

  g_mutex_lock (&enc->eos_lock);
  if (enc->eos) {
    GST_WARNING_OBJECT (enc, "got frame in eos state");
    g_mutex_unlock (&enc->eos_lock);
    ret = GST_FLOW_EOS;
    goto error;
  }
  g_mutex_unlock (&enc->eos_lock);

  /* Now we create the actual codec */
  if (G_UNLIKELY (enc->dirty)) {
    g_assert (enc->codec == NULL);

    if (!gst_droidvenc_create_codec (enc)) {
      goto error;
    }

    enc->dirty = FALSE;
  }

  gst_buffer_map (frame->input_buffer, &info, GST_MAP_READ);
  data.data.size = info.size;
  data.data.data = info.data;
  data.sync = false;
  data.ts = GST_TIME_AS_USECONDS (frame->pts);

  release_data = g_slice_new (GstDroidVEncFrameReleaseData);
  release_data->info = info;
  release_data->frame = gst_video_codec_frame_ref (frame);

  cb.unref = gst_droidvenc_release_input_frame;
  cb.data = release_data;

  /* This can deadlock if droidmedia/stagefright input buffer queue is full thus we
   * cannot write the input buffer. We end up waiting for the write operation
   * which does not happen because stagefright needs us to provide
   * output buffers to be filled (which can not happen because _loop() tries
   * to call get_oldest_frame() which acquires the stream lock the base class
   * is holding before calling us
   */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
  droid_media_codec_queue (enc->codec, &data, &cb);
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  if (enc->downstream_flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (enc, "not handling frame in error state: %s",
        gst_flow_get_name (enc->downstream_flow_ret));
    ret = enc->downstream_flow_ret;
    goto out;
  }

  g_mutex_lock (&enc->eos_lock);
  if (enc->eos) {
    GST_WARNING_OBJECT (enc, "got frame in eos state");
    g_mutex_unlock (&enc->eos_lock);
    ret = GST_FLOW_EOS;
    goto out;
  }
  g_mutex_unlock (&enc->eos_lock);

  ret = GST_FLOW_OK;

out:
  return ret;

error:
  /* don't leak the frame */
  gst_video_encoder_finish_frame (encoder, frame);

  return ret;
}

static gboolean
gst_droidvenc_flush (GstVideoEncoder * encoder)
{
  GstDroidVEnc *enc = GST_DROIDVENC (encoder);

  GST_DEBUG_OBJECT (enc, "flush");

  enc->downstream_flow_ret = GST_FLOW_OK;
  g_mutex_lock (&enc->eos_lock);
  enc->eos = FALSE;
  g_mutex_unlock (&enc->eos_lock);

  if (enc->codec) {
    GST_WARNING_OBJECT (enc, "encoder cannot be flushed!");
  }

  return TRUE;
}

static void
gst_droidvenc_init (GstDroidVEnc * enc)
{
  enc->codec = NULL;
  enc->codec_type = NULL;
  enc->in_state = NULL;
  enc->out_state = NULL;
  enc->target_bitrate = GST_DROID_ENC_TARGET_BITRATE_DEFAULT;
  enc->downstream_flow_ret = GST_FLOW_OK;
  g_mutex_init (&enc->eos_lock);
  g_cond_init (&enc->eos_cond);
}

static GstCaps *
gst_droidvenc_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  GstDroidVEnc *enc;
  GstCaps *caps;
  GstCaps *ret;

  enc = GST_DROIDVENC (encoder);

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
gst_droidvenc_class_init (GstDroidVEncClass * klass)
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

  caps = gst_droid_codec_get_all_caps (GST_DROID_CODEC_ENCODER_VIDEO);

  tpl = gst_pad_template_new (GST_VIDEO_ENCODER_SRC_NAME,
      GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (gstelement_class, tpl);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droidvenc_sink_template_factory));

  gobject_class->finalize = gst_droidvenc_finalize;
  gobject_class->set_property = gst_droidvenc_set_property;
  gobject_class->get_property = gst_droidvenc_get_property;

  gstvideoencoder_class->open = GST_DEBUG_FUNCPTR (gst_droidvenc_open);
  gstvideoencoder_class->close = GST_DEBUG_FUNCPTR (gst_droidvenc_close);
  gstvideoencoder_class->start = GST_DEBUG_FUNCPTR (gst_droidvenc_start);
  gstvideoencoder_class->stop = GST_DEBUG_FUNCPTR (gst_droidvenc_stop);
  gstvideoencoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_droidvenc_set_format);
  gstvideoencoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_droidvenc_getcaps);
  gstvideoencoder_class->finish = GST_DEBUG_FUNCPTR (gst_droidvenc_finish);
  gstvideoencoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_droidvenc_handle_frame);
  gstvideoencoder_class->flush = GST_DEBUG_FUNCPTR (gst_droidvenc_flush);

  g_object_class_install_property (gobject_class, PROP_TARGET_BITRATE,
      g_param_spec_int ("target-bitrate", "Target Bitrate",
          "Target bitrate", 0, G_MAXINT,
          GST_DROID_ENC_TARGET_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
