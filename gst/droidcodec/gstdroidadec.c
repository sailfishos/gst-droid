/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
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

#include "gstdroidadec.h"
#include "plugin.h"
#include <string.h>             /* memset() */
#ifdef HAVE_ORC
#include <orc/orc.h>
#else
#define orc_memcpy memcpy
#endif

#define gst_droidadec_parent_class parent_class
G_DEFINE_TYPE (GstDroidADec, gst_droidadec, GST_TYPE_AUDIO_DECODER);

GST_DEBUG_CATEGORY_EXTERN (gst_droid_adec_debug);
#define GST_CAT_DEFAULT gst_droid_adec_debug

static GstStaticPadTemplate gst_droidadec_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_AUDIO_DECODER_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE (GST_AUDIO_DEF_FORMAT)));

static void gst_droidadec_error (void *data, int err);
static void gst_droidadec_signal_eos (void *data);
static void gst_droidadec_data_available (void *data,
    DroidMediaCodecData * encoded);
static GstFlowReturn gst_droidadec_finish (GstAudioDecoder * decoder);

static gboolean
gst_droidadec_create_codec (GstDroidADec * dec, GstBuffer * input)
{
  DroidMediaCodecDecoderMetaData md;
  const gchar *droid = gst_droid_codec_get_droid_type (dec->codec_type);

  GST_INFO_OBJECT (dec, "create codec of type %s. Channels: %d, Rate: %d",
      droid, dec->channels, dec->rate);

  memset (&md, 0x0, sizeof (md));

  md.parent.type = droid;
  md.parent.channels = dec->channels;
  md.parent.sample_rate = dec->rate;
  md.parent.flags = DROID_MEDIA_CODEC_SW_ONLY;
  md.codec_data.size = 0;

  switch (gst_droid_codec_create_decoder_codec_data (dec->codec_type,
          dec->codec_data, &md.codec_data, input)) {
    case GST_DROID_CODEC_CODEC_DATA_OK:
      break;

    case GST_DROID_CODEC_CODEC_DATA_NOT_NEEDED:
      g_assert (dec->codec_data == NULL);
      break;

    case GST_DROID_CODEC_CODEC_DATA_ERROR:
      GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
          ("Failed to create codec_data."));
      return FALSE;
  }

  dec->codec = droid_media_codec_create_decoder (&md);

  if (md.codec_data.size > 0) {
    g_free (md.codec_data.data);
  }

  if (!dec->codec) {
    GST_ELEMENT_ERROR (dec, LIBRARY, SETTINGS, NULL,
        ("Failed to create decoder"));

    return FALSE;
  }

  {
    DroidMediaCodecCallbacks cb;
    cb.signal_eos = gst_droidadec_signal_eos;
    cb.error = gst_droidadec_error;
    cb.size_changed = NULL;
    droid_media_codec_set_callbacks (dec->codec, &cb, dec);
  }

  {
    DroidMediaCodecDataCallbacks cb;
    cb.data_available = gst_droidadec_data_available;
    droid_media_codec_set_data_callbacks (dec->codec, &cb, dec);
  }

  if (!droid_media_codec_start (dec->codec)) {
    GST_ELEMENT_ERROR (dec, LIBRARY, INIT, (NULL),
        ("Failed to start the decoder"));

    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;

    return FALSE;
  }

  dec->running = TRUE;

  return TRUE;
}

static void
gst_droidadec_data_available (void *data, DroidMediaCodecData * encoded)
{
  GstFlowReturn flow_ret;
  GstDroidADec *dec = (GstDroidADec *) data;
  GstAudioDecoder *decoder = GST_AUDIO_DECODER (dec);
  GstBuffer *out;
  GstMapInfo info;

  GST_DEBUG_OBJECT (dec, "data available of size %d", encoded->data.size);

  GST_AUDIO_DECODER_STREAM_LOCK (decoder);

  if (G_UNLIKELY (dec->downstream_flow_ret != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (dec, "not handling data in error state: %s",
        gst_flow_get_name (dec->downstream_flow_ret));
    flow_ret = dec->downstream_flow_ret;
    gst_audio_decoder_finish_frame (decoder, NULL, 1);
    goto out;
  }

  if (G_UNLIKELY (gst_audio_decoder_get_audio_info (GST_AUDIO_DECODER
              (dec))->finfo->format == GST_AUDIO_FORMAT_UNKNOWN)) {
    DroidMediaCodecMetaData md;
    DroidMediaRect crop;        /* TODO: get rid of that */
    GstAudioInfo info;

    memset (&md, 0x0, sizeof (md));
    droid_media_codec_get_output_info (dec->codec, &md, &crop);
    GST_INFO_OBJECT (dec, "output rate=%d, output channels=%d", md.sample_rate,
        md.channels);

    gst_audio_info_init (&info);
    gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, md.sample_rate,
        md.channels, NULL);

    if (!gst_audio_decoder_set_output_format (decoder, &info)) {
      flow_ret = GST_FLOW_ERROR;
      goto out;
    }

    dec->info = gst_audio_decoder_get_audio_info (GST_AUDIO_DECODER (dec));
  }

  out = gst_audio_decoder_allocate_output_buffer (decoder, encoded->data.size);

  gst_buffer_map (out, &info, GST_MAP_READWRITE);
  orc_memcpy (info.data, encoded->data.data, encoded->data.size);
  gst_buffer_unmap (out, &info);

  //  GST_WARNING_OBJECT (dec, "bpf %d, bps %d", dec->info->bpf, GST_AUDIO_INFO_BPS(dec->info));
  if (dec->spf == -1 || (encoded->data.size == dec->spf * dec->info->bpf
          && gst_adapter_available (dec->adapter) == 0)) {
    /* fast path. no need for anything */
    goto push;
  }

  gst_adapter_push (dec->adapter, out);

  if (gst_adapter_available (dec->adapter) >= dec->spf * dec->info->bpf) {
    out = gst_adapter_take_buffer (dec->adapter, dec->spf * dec->info->bpf);
  } else {
    flow_ret = GST_FLOW_OK;
    goto out;
  }

push:
  GST_DEBUG_OBJECT (dec, "pushing %d bytes out", gst_buffer_get_size (out));

  flow_ret = gst_audio_decoder_finish_frame (decoder, out, 1);

  if (flow_ret == GST_FLOW_OK || flow_ret == GST_FLOW_FLUSHING) {
    goto out;
  } else if (flow_ret == GST_FLOW_EOS) {
    GST_INFO_OBJECT (dec, "eos");
  } else if (flow_ret < GST_FLOW_OK) {
    GST_ELEMENT_ERROR (dec, STREAM, FAILED,
        ("Internal data stream error."), ("stream stopped, reason %s",
            gst_flow_get_name (flow_ret)));
  }

out:
  dec->downstream_flow_ret = flow_ret;
  GST_AUDIO_DECODER_STREAM_UNLOCK (decoder);
}

static void
gst_droidadec_signal_eos (void *data)
{
  GstDroidADec *dec = (GstDroidADec *) data;

  GST_DEBUG_OBJECT (dec, "codec signaled EOS");

  g_mutex_lock (&dec->eos_lock);

  if (!dec->eos) {
    GST_WARNING_OBJECT (dec, "codec signaled EOS but we are not expecting it");
  }

  g_cond_signal (&dec->eos_cond);
  g_mutex_unlock (&dec->eos_lock);
}

static void
gst_droidadec_error (void *data, int err)
{
  GstDroidADec *dec = (GstDroidADec *) data;

  GST_DEBUG_OBJECT (dec, "codec error");

  GST_AUDIO_DECODER_STREAM_LOCK (dec);
  dec->running = FALSE;
  GST_AUDIO_DECODER_STREAM_UNLOCK (dec);

  g_mutex_lock (&dec->eos_lock);

  if (dec->eos) {
    /* Gotta love Android. We will ignore errors if we are expecting EOS */
    g_cond_signal (&dec->eos_cond);
    goto out;
  }

  GST_AUDIO_DECODER_STREAM_LOCK (dec);
  dec->downstream_flow_ret = GST_FLOW_ERROR;
  GST_AUDIO_DECODER_STREAM_UNLOCK (dec);

  GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL,
      ("error 0x%x from android codec", -err));

out:
  g_mutex_unlock (&dec->eos_lock);
}

static gboolean
gst_droidadec_stop (GstAudioDecoder * decoder)
{
  GstDroidADec *dec = GST_DROIDADEC (decoder);

  GST_DEBUG_OBJECT (dec, "stop");

  if (dec->codec) {
    droid_media_codec_stop (dec->codec);
    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;
  }

  gst_adapter_flush (dec->adapter, gst_adapter_available (dec->adapter));

  g_mutex_lock (&dec->eos_lock);
  dec->eos = FALSE;
  g_mutex_unlock (&dec->eos_lock);

  gst_buffer_replace (&dec->codec_data, NULL);

  if (dec->codec_type) {
    gst_droid_codec_unref (dec->codec_type);
    dec->codec_type = NULL;
  }

  return TRUE;
}

static void
gst_droidadec_finalize (GObject * object)
{
  GstDroidADec *dec = GST_DROIDADEC (object);

  GST_DEBUG_OBJECT (dec, "finalize");

  gst_droidadec_stop (GST_AUDIO_DECODER (dec));

  g_mutex_clear (&dec->eos_lock);
  g_cond_clear (&dec->eos_cond);

  gst_object_unref (dec->adapter);
  dec->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_droidadec_open (GstAudioDecoder * decoder)
{
  GstDroidADec *dec = GST_DROIDADEC (decoder);

  GST_DEBUG_OBJECT (dec, "open");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droidadec_close (GstAudioDecoder * decoder)
{
  GstDroidADec *dec = GST_DROIDADEC (decoder);

  GST_DEBUG_OBJECT (dec, "close");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droidadec_start (GstAudioDecoder * decoder)
{
  GstDroidADec *dec = GST_DROIDADEC (decoder);

  GST_DEBUG_OBJECT (dec, "start");

  dec->eos = FALSE;
  dec->downstream_flow_ret = GST_FLOW_OK;
  dec->codec_type = NULL;
  dec->dirty = TRUE;
  dec->spf = -1;
  dec->running = TRUE;

  return TRUE;
}

static gboolean
gst_droidadec_set_format (GstAudioDecoder * decoder, GstCaps * caps)
{
  GstDroidADec *dec = GST_DROIDADEC (decoder);
  GstStructure *str = gst_caps_get_structure (caps, 0);
  const GValue *value = gst_structure_get_value (str, "codec_data");
  GstBuffer *codec_data = value ? gst_value_get_buffer (value) : NULL;

  /*
   * destroying the droidmedia codec here will cause stagefright to call abort.
   * That is why we create it after we are sure that everything is correct
   */

  GST_DEBUG_OBJECT (dec, "set format %" GST_PTR_FORMAT, caps);

  if (dec->codec) {
    /* If we get a format change then we stop */
    GstCaps *current =
        gst_pad_get_current_caps (GST_AUDIO_DECODER_SINK_PAD (decoder));
    gboolean equal = gst_caps_is_equal_fixed (caps, current);
    gst_caps_unref (current);

    GST_DEBUG_OBJECT (dec, "new format is similar to old format? %d", equal);

    if (!equal) {
      GST_ELEMENT_ERROR (dec, LIBRARY, SETTINGS, (NULL),
          ("codec already configured"));
    }

    return equal;
  }

  dec->codec_type =
      gst_droid_codec_new_from_caps (caps, GST_DROID_CODEC_DECODER_AUDIO);
  if (!dec->codec_type) {
    GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, (NULL),
        ("Unknown codec type for caps %" GST_PTR_FORMAT, caps));
    return FALSE;
  }

  if (!gst_structure_get_int (str, "channels", &dec->channels) ||
      !gst_structure_get_int (str, "rate", &dec->rate)) {
    GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
        ("Failed to parse caps %" GST_PTR_FORMAT, caps));
    return FALSE;
  }

  GST_INFO_OBJECT (dec, "configuring decoder. rate=%d, channels=%d", dec->rate,
      dec->channels);

  gst_buffer_replace (&dec->codec_data, codec_data);

  /* handle_frame will create the codec */
  dec->dirty = TRUE;

  dec->spf = gst_droid_codec_get_samples_per_frane (caps);

  GST_INFO_OBJECT (dec, "samples per frame: %d", dec->spf);

  return TRUE;
}

/* always call with stream lock */
static GstFlowReturn
gst_droidadec_finish (GstAudioDecoder * decoder)
{
  gboolean locked = FALSE;      /* TODO: This is a hack */
  GstDroidADec *dec = GST_DROIDADEC (decoder);
  gint available;

  GST_DEBUG_OBJECT (dec, "finish");

  if (!dec->running) {
    GST_DEBUG_OBJECT (dec, "decoder is not running");
    goto finish;
  }

  g_mutex_lock (&dec->eos_lock);
  locked = TRUE;
  dec->eos = TRUE;

  if (dec->codec) {
    droid_media_codec_drain (dec->codec);
  } else {
    goto out;
  }

  /* release the lock to allow _data_available () to do its job */
  GST_AUDIO_DECODER_STREAM_UNLOCK (decoder);
  /* Now we wait for the codec to signal EOS */
  g_cond_wait (&dec->eos_cond, &dec->eos_lock);
  GST_AUDIO_DECODER_STREAM_LOCK (decoder);

finish:
  /* We drained the codec. Better to recreate it. */
  if (dec->codec) {
    droid_media_codec_stop (dec->codec);
    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;
  }

  if (dec->spf != -1) {
    available = gst_adapter_available (dec->adapter);
    if (available > 0) {
      gint size = dec->spf * dec->info->bpf;
      gint nframes = available / size;
      GstBuffer *out;
      GstFlowReturn ret G_GNUC_UNUSED;

      GST_INFO_OBJECT (dec, "pushing remaining %d bytes", available);
      if (nframes > 0) {
        out = gst_adapter_take_buffer (dec->adapter, nframes * size);
        available -= (nframes * size);
      } else {
        out = gst_adapter_take_buffer (dec->adapter, available);
        nframes = 1;
        available = 0;
      }

      ret = gst_audio_decoder_finish_frame (decoder, out, nframes);


      GST_INFO_OBJECT (dec, "pushed %d frames. flow return: %s", nframes,
          gst_flow_get_name (ret));

      if (available > 0) {
        GST_ERROR_OBJECT (dec, "%d bytes remaining", available);
      }
    }
  }

  dec->dirty = TRUE;

out:
  dec->eos = FALSE;

  if (locked) {
    g_mutex_unlock (&dec->eos_lock);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_droidadec_handle_frame (GstAudioDecoder * decoder, GstBuffer * buffer)
{
  GstDroidADec *dec = GST_DROIDADEC (decoder);
  GstFlowReturn ret;
  DroidMediaCodecData data;
  DroidMediaBufferCallbacks cb;

  GST_DEBUG_OBJECT (dec, "handle frame");

  if (G_UNLIKELY (!buffer)) {
    return gst_droidadec_finish (decoder);
  }

  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (dec, "not handling frame in error state: %s",
        gst_flow_get_name (dec->downstream_flow_ret));
    ret = dec->downstream_flow_ret;
    goto error;
  }

  g_mutex_lock (&dec->eos_lock);
  if (dec->eos) {
    GST_WARNING_OBJECT (dec, "got frame in eos state");
    g_mutex_unlock (&dec->eos_lock);
    ret = GST_FLOW_EOS;
    goto error;
  }
  g_mutex_unlock (&dec->eos_lock);

  /* We must create the codec before we process any data. _create_codec will call
   * construct_decoder_codec_data which will store the nal prefix length for H264.
   * This is a bad situation. TODO: fix it
   */
  if (G_UNLIKELY (dec->dirty)) {
    if (dec->codec) {
      gst_droidadec_finish (decoder);
    }

    if (!gst_droidadec_create_codec (dec, buffer)) {
      ret = GST_FLOW_ERROR;
      goto error;
    }

    dec->dirty = FALSE;
  }

  if (!gst_droid_codec_process_decoder_data (dec->codec_type, buffer,
          &data.data)) {
    /* TODO: error */
    ret = GST_FLOW_ERROR;
    goto error;
  }

  cb.unref = g_free;
  cb.data = data.data.data;

  GST_DEBUG_OBJECT (dec, "decoding data of size %d (%d)",
      gst_buffer_get_size (buffer), data.data.size);

  /*
   * We are ignoring timestamping completely and relying
   * on the base class to do our bookkeeping ;-)
   */
  data.ts = 0;
  data.sync = false;

  /* This can deadlock if droidmedia/stagefright input buffer queue is full thus we
   * cannot write the input buffer. We end up waiting for the write operation
   * which does not happen because stagefright needs us to provide
   * output buffers to be filled (which can not happen because _loop() tries
   * to call get_oldest_frame() which acquires the stream lock the base class
   * is holding before calling us
   */
  GST_AUDIO_DECODER_STREAM_UNLOCK (decoder);
  droid_media_codec_queue (dec->codec, &data, &cb);
  GST_AUDIO_DECODER_STREAM_LOCK (decoder);

  /* from now on decoder owns a frame reference so we cannot use the out label otherwise
   * we will drop the needed reference
   */

  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (dec, "not handling frame in error state: %s",
        gst_flow_get_name (dec->downstream_flow_ret));
    ret = dec->downstream_flow_ret;
    goto out;
  }

  g_mutex_lock (&dec->eos_lock);
  if (dec->eos) {
    GST_WARNING_OBJECT (dec, "got frame in eos state");
    g_mutex_unlock (&dec->eos_lock);
    ret = GST_FLOW_EOS;
    goto out;
  }
  g_mutex_unlock (&dec->eos_lock);

  ret = GST_FLOW_OK;

out:
  return ret;

error:
  /* don't leak the frame */
  gst_audio_decoder_finish_frame (decoder, NULL, 1);

  return ret;
}

static void
gst_droidadec_flush (GstAudioDecoder * decoder, gboolean hard)
{
  GstDroidADec *dec = GST_DROIDADEC (decoder);

  GST_DEBUG_OBJECT (dec, "flush %d", hard);

  if (hard) {
    gst_droidadec_finish (decoder);
  }

  dec->downstream_flow_ret = GST_FLOW_OK;
  g_mutex_lock (&dec->eos_lock);
  dec->eos = FALSE;
  g_mutex_unlock (&dec->eos_lock);
}

static void
gst_droidadec_init (GstDroidADec * dec)
{
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (dec), TRUE);
  gst_audio_decoder_set_drainable (GST_AUDIO_DECODER (dec), TRUE);

  dec->codec = NULL;
  dec->codec_type = NULL;
  dec->downstream_flow_ret = GST_FLOW_OK;
  dec->eos = FALSE;
  dec->codec_data = NULL;
  dec->channels = 0;
  dec->rate = 0;

  g_mutex_init (&dec->eos_lock);
  g_cond_init (&dec->eos_cond);
  dec->adapter = gst_adapter_new ();
}

static void
gst_droidadec_class_init (GstDroidADecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAudioDecoderClass *gstaudiodecoder_class;
  GstCaps *caps;
  GstPadTemplate *tpl;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstaudiodecoder_class = (GstAudioDecoderClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Audio decoder", "Decoder/Audio/Device",
      "Android HAL decoder", "Mohammed Sameer <msameer@foolab.org>");

  caps = gst_droid_codec_get_all_caps (GST_DROID_CODEC_DECODER_AUDIO);
  tpl = gst_pad_template_new (GST_AUDIO_DECODER_SINK_NAME,
      GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (gstelement_class, tpl);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droidadec_src_template_factory));

  gobject_class->finalize = gst_droidadec_finalize;

  gstaudiodecoder_class->open = GST_DEBUG_FUNCPTR (gst_droidadec_open);
  gstaudiodecoder_class->close = GST_DEBUG_FUNCPTR (gst_droidadec_close);
  gstaudiodecoder_class->start = GST_DEBUG_FUNCPTR (gst_droidadec_start);
  gstaudiodecoder_class->stop = GST_DEBUG_FUNCPTR (gst_droidadec_stop);
  gstaudiodecoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_droidadec_set_format);
  gstaudiodecoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_droidadec_handle_frame);
  gstaudiodecoder_class->flush = GST_DEBUG_FUNCPTR (gst_droidadec_flush);
}
