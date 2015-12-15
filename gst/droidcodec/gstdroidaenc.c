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

#include "gstdroidaenc.h"
#include "plugin.h"
#include <string.h>

#define gst_droidaenc_parent_class parent_class
G_DEFINE_TYPE (GstDroidAEnc, gst_droidaenc, GST_TYPE_AUDIO_ENCODER);

GST_DEBUG_CATEGORY_EXTERN (gst_droid_aenc_debug);
#define GST_CAT_DEFAULT gst_droid_aenc_debug

static GstStaticPadTemplate gst_droidaenc_sink_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_AUDIO_ENCODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE (GST_AUDIO_DEF_FORMAT)));

enum
{
  PROP_0,
  PROP_TARGET_BITRATE,
};

#define GST_DROID_A_ENC_TARGET_BITRATE_DEFAULT 128000

static void gst_droidaenc_signal_eos (void *data);
static void gst_droidaenc_error (void *data, int err);
static void gst_droidaenc_data_available (void *data,
    DroidMediaCodecData * encoded);

static gboolean
gst_droidaenc_negotiate_src_caps (GstDroidAEnc * enc, GstAudioInfo * info)
{
  GstCaps *caps, *tpl;

  GST_DEBUG_OBJECT (enc, "negotiate src caps");

  tpl = gst_droid_codec_get_all_caps (GST_DROID_CODEC_ENCODER_AUDIO);

  caps = gst_pad_peer_query_caps (GST_AUDIO_ENCODER_SRC_PAD (GST_AUDIO_ENCODER
          (enc)), tpl);

  gst_caps_unref (tpl);
  GST_LOG_OBJECT (enc, "peer caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_truncate (caps);

  enc->codec_type =
      gst_droid_codec_new_from_caps (caps, GST_DROID_CODEC_ENCODER_AUDIO);
  if (!enc->codec_type) {
    GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, (NULL),
        ("Unknown codec type for caps %" GST_PTR_FORMAT, caps));

    gst_caps_unref (caps);
    goto error;
  }

  gst_caps_set_simple (caps, "channels", G_TYPE_INT, info->channels, "rate",
      G_TYPE_INT, info->rate, NULL);

  gst_caps_replace (&enc->caps, caps);
  gst_caps_unref (caps);

  return TRUE;

error:
  return FALSE;
}

static gboolean
gst_droidaenc_create_codec (GstDroidAEnc * enc)
{
  DroidMediaCodecEncoderMetaData md;
  GstAudioInfo info;

  memset (&md, 0x0, sizeof (md));

  gst_audio_info_init (&info);
  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, enc->rate,
      enc->channels, NULL);
  const gchar *droid = gst_droid_codec_get_droid_type (enc->codec_type);

  GST_INFO_OBJECT (enc, "create codec of type: %s", droid);

  md.parent.type = droid;
  md.parent.channels = enc->channels;
  md.parent.sample_rate = enc->rate;
  md.parent.flags = DROID_MEDIA_CODEC_SW_ONLY;
  md.meta_data = false;
  md.bitrate = enc->target_bitrate;
  md.max_input_size = info.bpf * enc->rate;
  enc->codec = droid_media_codec_create_encoder (&md);

  if (!enc->codec) {
    GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, NULL,
        ("Failed to create encoder"));
    return FALSE;
  }

  {
    DroidMediaCodecCallbacks cb;
    cb.signal_eos = gst_droidaenc_signal_eos;
    cb.error = gst_droidaenc_error;
    droid_media_codec_set_callbacks (enc->codec, &cb, enc);
  }

  {
    DroidMediaCodecDataCallbacks cb;
    cb.data_available = gst_droidaenc_data_available;
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
gst_droidaenc_signal_eos (void *data)
{
  GstDroidAEnc *enc = (GstDroidAEnc *) data;

  GST_DEBUG_OBJECT (enc, "codec signaled EOS");

  g_mutex_lock (&enc->eos_lock);

  if (!enc->eos) {
    GST_WARNING_OBJECT (enc, "codec signaled EOS but we are not expecting it");
  }

  g_cond_signal (&enc->eos_cond);
  g_mutex_unlock (&enc->eos_lock);
}

static void
gst_droidaenc_error (void *data, int err)
{
  GstDroidAEnc *enc = (GstDroidAEnc *) data;

  GST_DEBUG_OBJECT (enc, "codec error");

  g_mutex_lock (&enc->eos_lock);

  if (enc->eos) {
    /* Gotta love Android. We will ignore errors if we are expecting EOS */
    g_cond_signal (&enc->eos_cond);
    goto out;
  }

  GST_AUDIO_ENCODER_STREAM_LOCK (enc);
  enc->downstream_flow_ret = GST_FLOW_ERROR;
  GST_AUDIO_ENCODER_STREAM_UNLOCK (enc);

  GST_ELEMENT_ERROR (enc, LIBRARY, FAILED, NULL,
      ("error 0x%x from android codec", -err));

out:
  g_mutex_unlock (&enc->eos_lock);
}

static void
gst_droidaenc_data_available (void *data, DroidMediaCodecData * encoded)
{
  GstFlowReturn flow_ret;
  GstDroidAEnc *enc = (GstDroidAEnc *) data;
  GstAudioEncoder *encoder = GST_AUDIO_ENCODER (enc);
  GstBuffer *buffer;

  GST_DEBUG_OBJECT (enc, "data available");

  GST_AUDIO_ENCODER_STREAM_LOCK (encoder);

  if (encoded->codec_config) {
    GstBuffer *codec_data = NULL;

    GST_INFO_OBJECT (enc, "received codec_data");

    if (G_UNLIKELY (enc->first_frame_sent)) {
      enc->downstream_flow_ret = GST_FLOW_ERROR;
      GST_AUDIO_ENCODER_STREAM_UNLOCK (encoder);

      GST_ELEMENT_ERROR (enc, STREAM, FORMAT, (NULL),
          ("codec data received more than once"));

      return;
    }

    codec_data =
        gst_droid_codec_create_encoder_codec_data (enc->codec_type,
        &encoded->data);

    if (!codec_data) {
      enc->downstream_flow_ret = GST_FLOW_ERROR;

      GST_AUDIO_ENCODER_STREAM_UNLOCK (encoder);

      GST_ELEMENT_ERROR (enc, STREAM, FORMAT, (NULL),
          ("Failed to construct codec_data. Expect corrupted stream"));

      return;
    }

    enc->first_frame_sent = TRUE;

    gst_caps_set_simple (enc->caps,
        "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
    gst_buffer_unref (codec_data);

    if (!gst_audio_encoder_set_output_format (GST_AUDIO_ENCODER (enc),
            enc->caps)) {
      enc->downstream_flow_ret = GST_FLOW_ERROR;

      GST_AUDIO_ENCODER_STREAM_UNLOCK (encoder);

      GST_ELEMENT_ERROR (enc, STREAM, FORMAT, (NULL),
          ("failed to set output caps"));
      return;
    }

    gst_caps_replace (&enc->caps, NULL);

    GST_AUDIO_ENCODER_STREAM_UNLOCK (encoder);
    return;
  }

  buffer =
      gst_audio_encoder_allocate_output_buffer (GST_AUDIO_ENCODER (enc),
      encoded->data.size);
  gst_buffer_fill (buffer, 0, encoded->data.data, encoded->data.size);

  GST_BUFFER_PTS (buffer) = encoded->ts;
  GST_BUFFER_DTS (buffer) = encoded->decoding_ts;

  /*
   * 1024 seems to be the number of samples per buffer that Android uses.
   * It should be fine as long as we have only AAC encoding but should
   * be revisited if we ever use another audio encoding format
   */
  flow_ret =
      gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (enc), buffer, 1024);

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
  GST_AUDIO_ENCODER_STREAM_UNLOCK (encoder);
}

static void
gst_droidaenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDroidAEnc *enc = GST_DROIDAENC (object);

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
gst_droidaenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDroidAEnc *enc = GST_DROIDAENC (object);

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
gst_droidaenc_finalize (GObject * object)
{
  GstDroidAEnc *enc = GST_DROIDAENC (object);

  GST_DEBUG_OBJECT (enc, "finalize");

  enc->codec = NULL;

  g_mutex_clear (&enc->eos_lock);
  g_cond_clear (&enc->eos_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_droidaenc_open (GstAudioEncoder * encoder)
{
  GstDroidAEnc *enc = GST_DROIDAENC (encoder);

  GST_DEBUG_OBJECT (enc, "open");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droidaenc_close (GstAudioEncoder * encoder)
{
  GstDroidAEnc *enc = GST_DROIDAENC (encoder);

  GST_DEBUG_OBJECT (enc, "close");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droidaenc_start (GstAudioEncoder * encoder)
{
  GstDroidAEnc *enc = GST_DROIDAENC (encoder);

  GST_DEBUG_OBJECT (enc, "start");

  enc->eos = FALSE;
  enc->downstream_flow_ret = GST_FLOW_OK;
  enc->dirty = TRUE;
  enc->finished = FALSE;

  return TRUE;
}

static gboolean
gst_droidaenc_stop (GstAudioEncoder * encoder)
{
  GstDroidAEnc *enc = GST_DROIDAENC (encoder);

  GST_DEBUG_OBJECT (enc, "stop");

  if (enc->codec) {
    droid_media_codec_stop (enc->codec);
    droid_media_codec_destroy (enc->codec);
    enc->codec = NULL;
    enc->dirty = TRUE;
  }

  if (enc->codec_type) {
    gst_droid_codec_unref (enc->codec_type);
    enc->codec_type = NULL;
  }

  gst_caps_replace (&enc->caps, NULL);

  return TRUE;
}

static gboolean
gst_droidaenc_set_format (GstAudioEncoder * encoder, GstAudioInfo * info)
{
  GstDroidAEnc *enc = GST_DROIDAENC (encoder);

  GST_DEBUG_OBJECT (enc, "set format");

  if (enc->codec) {
    GST_FIXME_OBJECT (enc, "What to do here?");
    GST_ERROR_OBJECT (enc, "codec already renegotiate");
    return FALSE;
  }

  enc->first_frame_sent = FALSE;

  enc->channels = info->channels;
  enc->rate = info->rate;

  if (!gst_droidaenc_negotiate_src_caps (enc, info)) {
    return FALSE;
  }

  /* handle_frame will create the codec */
  enc->dirty = TRUE;

  return TRUE;
}

static GstFlowReturn
gst_droidaenc_finish (GstAudioEncoder * encoder)
{
  GstDroidAEnc *enc = GST_DROIDAENC (encoder);

  GST_DEBUG_OBJECT (enc, "finish");

  g_mutex_lock (&enc->eos_lock);
  enc->eos = TRUE;

  if (enc->codec) {
    droid_media_codec_drain (enc->codec);
  } else {
    goto out;
  }

  /* release the lock to allow _frame_available () to do its job */
  GST_AUDIO_ENCODER_STREAM_UNLOCK (encoder);
  /* Now we wait for the codec to signal EOS */
  g_cond_wait (&enc->eos_cond, &enc->eos_lock);
  GST_AUDIO_ENCODER_STREAM_LOCK (encoder);

  enc->finished = TRUE;

out:
  enc->eos = FALSE;

  g_mutex_unlock (&enc->eos_lock);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_droidaenc_handle_frame (GstAudioEncoder * encoder, GstBuffer * buffer)
{
  GstDroidAEnc *enc = GST_DROIDAENC (encoder);
  GstFlowReturn ret = GST_FLOW_ERROR;
  DroidMediaCodecData data;
  GstMapInfo info;
  DroidMediaBufferCallbacks cb;

  GST_DEBUG_OBJECT (enc, "handle frame");

  if (G_UNLIKELY (!buffer)) {
    if (enc->finished) {
      return GST_FLOW_OK;
    }

    return gst_droidaenc_finish (encoder);
  }

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

    if (!gst_droidaenc_create_codec (enc)) {
      goto error;
    }

    enc->dirty = FALSE;
  }

  enc->finished = FALSE;

  gst_buffer_map (buffer, &info, GST_MAP_READ);
  data.data.size = info.size;
  data.data.data = g_malloc (info.size);
  data.sync = false;
  data.ts = GST_TIME_AS_USECONDS (buffer->pts);

  memcpy (data.data.data, info.data, info.size);

  gst_buffer_unmap (buffer, &info);

  cb.unref = g_free;
  cb.data = data.data.data;

  /* This can deadlock if droidmedia/stagefright input buffer queue is full thus we
   * cannot write the input buffer. We end up waiting for the write operation
   * which does not happen because stagefright needs us to provide
   * output buffers to be filled (which can not happen because _loop() tries
   * to call get_oldest_frame() which acquires the stream lock the base class
   * is holding before calling us
   */
  GST_AUDIO_ENCODER_STREAM_UNLOCK (encoder);
  droid_media_codec_queue (enc->codec, &data, &cb);
  GST_AUDIO_ENCODER_STREAM_LOCK (encoder);

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
  gst_audio_encoder_finish_frame (encoder, NULL, 1);

  return ret;
}

static void
gst_droidaenc_flush (GstAudioEncoder * encoder)
{
  GstDroidAEnc *enc = GST_DROIDAENC (encoder);

  GST_DEBUG_OBJECT (enc, "flush");

  enc->downstream_flow_ret = GST_FLOW_OK;
  g_mutex_lock (&enc->eos_lock);
  enc->eos = FALSE;
  g_mutex_unlock (&enc->eos_lock);

  if (enc->codec) {
    GST_WARNING_OBJECT (enc, "encoder cannot be flushed!");
  }
}

static void
gst_droidaenc_init (GstDroidAEnc * enc)
{
  enc->codec = NULL;
  enc->codec_type = NULL;
  enc->target_bitrate = GST_DROID_A_ENC_TARGET_BITRATE_DEFAULT;
  enc->downstream_flow_ret = GST_FLOW_OK;
  enc->channels = 0;
  enc->rate = 0;
  enc->caps = NULL;
  enc->finished = FALSE;

  g_mutex_init (&enc->eos_lock);
  g_cond_init (&enc->eos_cond);
}

static void
gst_droidaenc_class_init (GstDroidAEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAudioEncoderClass *gstaudioencoder_class;
  GstCaps *caps;
  GstPadTemplate *tpl;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstaudioencoder_class = (GstAudioEncoderClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Audio encoder", "Encoder/Audio/Device",
      "Android HAL encoder", "Mohammed Sameer <msameer@foolab.org>");

  caps = gst_droid_codec_get_all_caps (GST_DROID_CODEC_ENCODER_AUDIO);

  tpl = gst_pad_template_new (GST_AUDIO_ENCODER_SRC_NAME,
      GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (gstelement_class, tpl);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droidaenc_sink_template_factory));

  gobject_class->finalize = gst_droidaenc_finalize;
  gobject_class->set_property = gst_droidaenc_set_property;
  gobject_class->get_property = gst_droidaenc_get_property;

  gstaudioencoder_class->open = GST_DEBUG_FUNCPTR (gst_droidaenc_open);
  gstaudioencoder_class->close = GST_DEBUG_FUNCPTR (gst_droidaenc_close);
  gstaudioencoder_class->start = GST_DEBUG_FUNCPTR (gst_droidaenc_start);
  gstaudioencoder_class->stop = GST_DEBUG_FUNCPTR (gst_droidaenc_stop);
  gstaudioencoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_droidaenc_set_format);
  gstaudioencoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_droidaenc_handle_frame);
  gstaudioencoder_class->flush = GST_DEBUG_FUNCPTR (gst_droidaenc_flush);

  g_object_class_install_property (gobject_class, PROP_TARGET_BITRATE,
      g_param_spec_int ("target-bitrate", "Target Bitrate",
          "Target bitrate", 0, G_MAXINT,
          GST_DROID_A_ENC_TARGET_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
