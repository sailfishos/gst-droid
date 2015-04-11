/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
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

#include "gstdroidadec.h"
#include "plugin.h"

#ifdef HAVE_ORC
#include <orc/orc.h>
#else
#define orc_memcpy memcpy
#endif

#define gst_droidadec_parent_class parent_class
G_DEFINE_TYPE (GstDroidADec, gst_droidadec, GST_TYPE_AUDIO_DECODER);

GST_DEBUG_CATEGORY_EXTERN (gst_droid_dec_debug);
#define GST_CAT_DEFAULT gst_droid_dec_debug

static GstStaticPadTemplate gst_droidadec_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_AUDIO_DECODER_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_CAPS_MAKE (GST_AUDIO_DEF_FORMAT)));
#if 0
static GstVideoCodecState *gst_droiddec_configure_state (GstVideoDecoder *
    decoder, gsize width, gsize height);
#endif
static void gst_droidadec_error (void *data, int err);
static void gst_droidadec_signal_eos (void *data);
#if 0
static void gst_droiddec_buffers_released (void *user);
static void gst_droiddec_frame_available (void *user);
#endif
static void gst_droidadec_data_available (void *data,
    DroidMediaCodecData * encoded);
#if 0
static gboolean gst_droiddec_convert_buffer (GstDroidDec * dec,
    DroidMediaBuffer * in, GstBuffer * out, GstVideoInfo * info);
#endif
static GstFlowReturn gst_droidadec_finish (GstAudioDecoder * decoder);

static gboolean
gst_droidadec_create_codec (GstDroidADec * dec)
{
  DroidMediaCodecDecoderMetaData md;
  const gchar *droid = gst_droid_codec_get_droid_type (dec->codec_type);

  GST_INFO_OBJECT (dec, "create codec of type %s", droid);

  md.parent.type = droid;
  md.parent.channels = dec->channels;
  md.parent.sample_rate = dec->rate;
  md.parent.flags = DROID_MEDIA_CODEC_SW_ONLY;
  md.codec_data.size = 0;

  if (dec->codec_data) {
    if (!gst_droid_codec_create_decoder_codec_data (dec->codec_type,
            dec->codec_data, &md.codec_data)) {
      GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
          ("Failed to create codec_data."));

      return FALSE;
    }
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

  dec->queue = droid_media_codec_get_buffer_queue (dec->codec);

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
    dec->queue = NULL;

    return FALSE;
  }

  return TRUE;
}

#if 0
static void
gst_droiddec_buffers_released (G_GNUC_UNUSED void *user)
{
  GST_FIXME ("Not sure what to do here really");
}

static gboolean
gst_droiddec_convert_buffer (GstDroidDec * dec, DroidMediaBuffer * in,
    GstBuffer * out, GstVideoInfo * info)
{
  GstMapInfo map_info;
  gpointer data;
  gboolean fix = FALSE;
  gboolean ret = FALSE;
  gsize width;
  gsize height = info->height;

  GST_DEBUG_OBJECT (dec, "convert buffer");

  width = droid_media_buffer_get_width (in);

  if (!gst_buffer_map (out, &map_info, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (dec, "failed to map buffer");
    return FALSE;
  }

  data = map_info.data;

  if (GST_VIDEO_INFO_COMP_STRIDE (info, 0) != width) {
    fix = TRUE;
    /* This is probably larger than we need but I don't know of a way to
     * calculate a proper size */
    data = g_malloc (gst_buffer_get_size (out));
  }

  if (droid_media_convert_to_i420 (dec->convert, in, data) != true) {
    GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, (NULL),
        ("failed to convert frame"));

    goto out;
  }

  if (fix) {
    /* copy the data */
    /* based on gst-colorconv commit 16fbaa7770f36d94163a9cac62b7bcbd7f9dda91 */

    gint stride = GST_VIDEO_INFO_COMP_STRIDE (info, 0);
    gint strideUV = GST_VIDEO_INFO_COMP_STRIDE (info, 1);
    guint8 *p = data;
    guint8 *dst = map_info.data;
    int i;
    int x;

    /* Y */
    for (i = height; i > 0; i--) {
      orc_memcpy (dst, p, width);
      dst += stride;
      p += width;
    }

    /* U and V */
    for (x = 0; x < 2; x++) {
      for (i = height / 2; i > 0; i--) {
        orc_memcpy (dst, p, width / 2);
        dst += strideUV;
        p += width / 2;
      }
    }
  }

  ret = TRUE;

out:
  if (fix) {
    g_free (data);
  }

  gst_buffer_unmap (out, &map_info);

  return ret;
}

#endif
static void
gst_droidadec_data_available (void *data, DroidMediaCodecData * encoded)
{
  GstFlowReturn flow_ret;
  GstDroidADec *dec = (GstDroidADec *) data;
  GstAudioDecoder *decoder = GST_AUDIO_DECODER (dec);
  GstBuffer *out;
  GstMapInfo info;

  GST_DEBUG_OBJECT (dec, "data available");

  GST_AUDIO_DECODER_STREAM_LOCK (decoder);

  out = gst_audio_decoder_allocate_output_buffer (decoder, encoded->data.size);

  gst_buffer_map (out, &info, GST_MAP_READWRITE);
  memcpy (info.data, encoded->data.data, encoded->data.size);
  gst_buffer_unmap (out, &info);

  GST_BUFFER_PTS (out) = encoded->ts;
  GST_BUFFER_DTS (out) = encoded->decoding_ts;

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

#if 0
static void
gst_droiddec_frame_available (void *user)
{
  GstDroidDec *dec = (GstDroidDec *) user;
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
  GstMemory *mem;
  guint width, height;
  GstVideoCodecFrame *frame;
  DroidMediaBuffer *buffer;
  GstBuffer *buff;
  DroidMediaRect rect;
  GstVideoCropMeta *crop_meta;
  DroidMediaBufferCallbacks cb;
  GstFlowReturn flow_ret;
  int64_t ts;

  GST_DEBUG_OBJECT (dec, "frame available");

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (dec->dirty) {
    goto acquire_and_release;
  }

  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (dec, "not handling frame in error state: %s",
        gst_flow_get_name (dec->downstream_flow_ret));
    goto acquire_and_release;
  }

  if (dec->convert) {
    buff = gst_video_decoder_allocate_output_buffer (decoder);
  } else {
    buff = gst_buffer_new ();
  }

  cb.ref = (DroidMediaCallback) gst_buffer_ref;
  cb.unref = (DroidMediaCallback) gst_buffer_unref;
  cb.data = buff;

  mem =
      gst_droid_media_buffer_allocator_alloc (dec->allocator, dec->queue, &cb);

  if (!mem) {
    /* TODO: what should we do here? */
    GST_ERROR_OBJECT (dec, "failed to acquire buffer from droidmedia");
    gst_buffer_unref (buff);
    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    return;
  }

  buffer = gst_droid_media_buffer_memory_get_buffer (mem);
  width = droid_media_buffer_get_width (buffer);
  height = droid_media_buffer_get_height (buffer);
  rect = droid_media_buffer_get_crop_rect (buffer);
  ts = droid_media_buffer_get_timestamp (buffer);

  if (dec->convert) {
    gboolean result =
        gst_droiddec_convert_buffer (dec, buffer, buff, &dec->out_state->info);

    gst_memory_unref (mem);
    mem = NULL;
    buffer = NULL;

    if (!result) {
      flow_ret = GST_FLOW_ERROR;
      gst_buffer_unref (buff);
      goto out;
    }
  } else {
    gst_buffer_insert_memory (buff, 0, mem);
  }

  /* we don't want to access the memory afterwards */
  mem = NULL;
  buffer = NULL;

  crop_meta = gst_buffer_add_video_crop_meta (buff);
  crop_meta->x = rect.left;
  crop_meta->y = rect.top;
  crop_meta->width = rect.right - rect.left;
  crop_meta->height = rect.bottom - rect.top;

  gst_buffer_add_video_meta (buff, GST_VIDEO_FRAME_FLAG_NONE,
      dec->convert ? GST_VIDEO_FORMAT_I420 : GST_VIDEO_FORMAT_YV12, width,
      height);

  frame = gst_video_decoder_get_oldest_frame (GST_VIDEO_DECODER (dec));

  if (G_UNLIKELY (!frame)) {
    /* TODO: what should we do here? */
    GST_WARNING_OBJECT (dec, "buffer without frame");
    gst_buffer_unref (buff);
    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    return;
  }

  frame->output_buffer = buff;

  /* We get the timestamp in ns already */
  frame->pts = ts;

  /* we have a ref so drop it */
  gst_video_codec_frame_unref (frame);

  flow_ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (dec), frame);

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
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  return;

acquire_and_release:
  /* we can not use our cb struct here so ask droidmedia to do
   * the work instead */
  droid_media_buffer_queue_acquire_and_release (dec->queue);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
}
#endif

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

#if 0
static int
gst_droiddec_size_changed (void *data, int32_t width, int32_t height)
{
  GstDroidDec *dec = (GstDroidDec *) data;
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
  int err = 0;

  GST_INFO_OBJECT (dec, "size changed: w=%d, h=%d", width, height);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (dec->out_state) {
    gst_video_codec_state_unref (dec->out_state);
  }

  dec->out_state = gst_droiddec_configure_state (decoder, width, height);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return err;
}

static GstVideoCodecState *
gst_droiddec_configure_state (GstVideoDecoder * decoder, gsize width,
    gsize height)
{
  GstVideoCodecState *out;
  GstDroidDec *dec = GST_DROIDDEC (decoder);
  GstCaps *caps, *tpl;
  GstVideoInfo info;
  GstVideoFormat fmt;

  GST_DEBUG_OBJECT (dec, "configure state: width: %d, height: %d",
      width, height);

  tpl = gst_pad_get_pad_template_caps (GST_VIDEO_DECODER_SRC_PAD (decoder));
  caps = gst_pad_peer_query_caps (GST_VIDEO_DECODER_SRC_PAD (decoder), tpl);
  gst_caps_unref (tpl);
  tpl = NULL;

  GST_DEBUG_OBJECT (dec, "peer caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_truncate (caps);
  caps = gst_caps_fixate (caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_WARNING_OBJECT (dec, "failed to parse caps");
    /* This will most likely fail but you never know */
    fmt = GST_VIDEO_FORMAT_YV12;
  } else {
    fmt = info.finfo->format;
  }

  out = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
      fmt, width, height, dec->in_state);

  gst_caps_unref (caps);

  return out;
}
#endif

static gboolean
gst_droidadec_stop (GstAudioDecoder * decoder)
{
  GstDroidADec *dec = GST_DROIDADEC (decoder);

  GST_DEBUG_OBJECT (dec, "stop");

  if (dec->codec) {
    droid_media_codec_stop (dec->codec);
    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;
    dec->queue = NULL;
  }

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

  return TRUE;
}

static gboolean
gst_droidadec_set_format (GstAudioDecoder * decoder, GstCaps * caps)
{
  GstDroidADec *dec = GST_DROIDADEC (decoder);
  GstStructure *str = gst_caps_get_structure (caps, 0);
  const GValue *value = gst_structure_get_value (str, "codec_data");
  GstBuffer *codec_data = gst_value_get_buffer (value);
  GstAudioInfo info;

  /*
   * destroying the droidmedia codec here will cause stagefright to call abort.
   * That is why we create it after we are sure that everything is correct
   */

  GST_DEBUG_OBJECT (dec, "set format %" GST_PTR_FORMAT, caps);

  if (dec->codec) {
    GST_FIXME_OBJECT (dec, "What to do here?");
    GST_ERROR_OBJECT (dec, "codec already configured");
    return FALSE;
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

  gst_audio_info_init (&info);
  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, dec->rate,
      dec->channels, NULL);

  if (!gst_audio_decoder_set_output_format (decoder, &info)) {
    return FALSE;
  }

  gst_buffer_replace (&dec->codec_data, codec_data);

  /* handle_frame will create the codec */
  dec->dirty = TRUE;

  return TRUE;
}

static GstFlowReturn
gst_droidadec_finish (GstAudioDecoder * decoder)
{
  GstDroidADec *dec = GST_DROIDADEC (decoder);

  GST_DEBUG_OBJECT (dec, "finish");

  g_mutex_lock (&dec->eos_lock);
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

  /* We drained the codec. Better to recreate it. */
  if (dec->codec) {
    droid_media_codec_stop (dec->codec);
    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;
    dec->queue = NULL;
  }

  dec->dirty = TRUE;

out:
  dec->eos = FALSE;

  g_mutex_unlock (&dec->eos_lock);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_droidadec_handle_frame (GstAudioDecoder * decoder, GstBuffer * buffer)
{
  GstDroidADec *dec = GST_DROIDADEC (decoder);
  GstFlowReturn ret;
  DroidMediaCodecData data;
  DroidMediaBufferCallbacks cb;
  GstMapInfo info;

  GST_DEBUG_OBJECT (dec, "handle frame");

  if (G_UNLIKELY (!buffer)) {
    return gst_droidadec_finish (decoder);
  }

  if (!GST_CLOCK_TIME_IS_VALID (buffer->dts)
      && !GST_CLOCK_TIME_IS_VALID (buffer->pts)) {
    GST_WARNING_OBJECT (dec,
        "dropping received frame with invalid timestamps.");
    ret = GST_FLOW_OK;
    goto error;
  }

  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (dec, "not handling frame in error state: %s",
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
    //    if (dec->codec) {
    //      gst_droidadec_finish (decoder);
    //    }

    if (!gst_droidadec_create_codec (dec)) {
      ret = GST_FLOW_ERROR;
      goto error;
    }

    dec->dirty = FALSE;
  }

  gst_buffer_map (buffer, &info, GST_MAP_READ);
  data.data.size = info.size;
  data.data.data = g_malloc (info.size);
  memcpy (data.data.data, info.data, info.size);
  gst_buffer_unmap (buffer, &info);

  cb.unref = g_free;
  cb.data = data.data.data;

  /*
   * try to use dts if pts is not valid.
   * on one of the test streams we get the first PTS set to GST_CLOCK_TIME_NONE
   * which breaks timestamping.
   */
  data.ts =
      GST_CLOCK_TIME_IS_VALID (buffer->
      pts) ? GST_TIME_AS_USECONDS (buffer->pts) : GST_TIME_AS_USECONDS (buffer->
      dts);

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
    GST_WARNING_OBJECT (dec, "not handling frame in error state: %s",
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

#if 0
static gboolean
gst_droiddec_flush (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "flush");

  /* We cannot flush the frames being decoded from the decoder. There is simply no way
   * to do that. The best we can do is to clear the queue of frames to be encoded.
   * The problem now is if we get flushed we will still decode the previous queued frames
   * and push them later on when they get decoded.
   * This will lead to frames being repeated if the flush happens in the beginning
   * or inaccurate seeking.
   * We will just mark the decoder as "dirty" so the next handle_frame can recreate it
   */

  dec->dirty = TRUE;

  dec->downstream_flow_ret = GST_FLOW_OK;
  g_mutex_lock (&dec->eos_lock);
  dec->eos = FALSE;
  g_mutex_unlock (&dec->eos_lock);

  return TRUE;
}
#endif

static void
gst_droidadec_init (GstDroidADec * dec)
{
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (dec), TRUE);

  dec->codec = NULL;
  dec->queue = NULL;
  dec->codec_type = NULL;
  dec->downstream_flow_ret = GST_FLOW_OK;
  dec->eos = FALSE;
  dec->codec_data = NULL;
  dec->channels = 0;
  dec->rate = 0;

  g_mutex_init (&dec->eos_lock);
  g_cond_init (&dec->eos_cond);
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
#if 0
  gstaudiodecoder_class->flush = GST_DEBUG_FUNCPTR (gst_droiddec_flush);
#endif
}
