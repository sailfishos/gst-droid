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

#include "gstdroiddec.h"
#include "gst/droid/gstdroidmediabuffer.h"
#include "plugin.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>

#define gst_droiddec_parent_class parent_class
G_DEFINE_TYPE (GstDroidDec, gst_droiddec, GST_TYPE_VIDEO_DECODER);

GST_DEBUG_CATEGORY_EXTERN (gst_droid_dec_debug);
#define GST_CAT_DEFAULT gst_droid_dec_debug

static GstStaticPadTemplate gst_droiddec_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_BUFFER, "{YV12}")));

static GstVideoCodecState *
gst_droiddec_configure_state (GstVideoDecoder * decoder, gsize width,
			      gsize height);
static void gst_droiddec_error (void *data, int err);
static int gst_droiddec_size_changed(void *data, int32_t width, int32_t height);
static void gst_droiddec_signal_eos (void *data);
static void gst_droiddec_buffers_released(void *user);
static void gst_droiddec_frame_available(void *user);

static gboolean
gst_droiddec_create_codec (GstDroidDec * dec)
{
  DroidMediaCodecDecoderMetaData md;

  GST_INFO_OBJECT (dec, "create codec of type %s: %dx%d",
      dec->codec_type->droid, dec->in_state->info.width, dec->in_state->info.height);

  md.parent.type = dec->codec_type->droid;
  md.parent.width = dec->in_state->info.width;
  md.parent.height = dec->in_state->info.height;
  md.parent.fps = dec->in_state->info.fps_n / dec->in_state->info.fps_d;
  md.parent.flags = DROID_MEDIA_CODEC_HW_ONLY;
  md.codec_data.size = 0;

  if (dec->codec_data) {
    g_assert (dec->codec_type->construct_decoder_codec_data);

    if (!dec->codec_type->construct_decoder_codec_data (dec->codec_data, &md.codec_data,
							&dec->codec_type_data)) {
      GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
			 ("Failed to construct codec_data."));

      return FALSE;
    }
  }

  dec->codec = droid_media_codec_create_decoder(&md);

  if (md.codec_data.size > 0) {
    g_free (md.codec_data.data);
  }

  if (!dec->codec) {
    GST_ELEMENT_ERROR(dec, LIBRARY, SETTINGS, NULL, ("Failed to create decoder"));

    return FALSE;
  }

  dec->queue = droid_media_codec_get_buffer_queue (dec->codec);

  {
    DroidMediaCodecCallbacks cb;
    cb.signal_eos = gst_droiddec_signal_eos;
    cb.error = gst_droiddec_error;
    cb.size_changed = gst_droiddec_size_changed;
    droid_media_codec_set_callbacks (dec->codec, &cb, dec);
  }

  {
    DroidMediaBufferQueueCallbacks cb;
    cb.buffers_released = gst_droiddec_buffers_released;
    cb.frame_available = gst_droiddec_frame_available;
    droid_media_buffer_queue_set_callbacks (dec->queue, &cb, dec);
  }

  if (!droid_media_codec_start (dec->codec)) {
    GST_ELEMENT_ERROR (dec, LIBRARY, INIT, (NULL), ("Failed to create a corresponding decoder"));

    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;
    dec->queue = NULL;

    return FALSE;
  }

  return TRUE;
}

static void
gst_droiddec_buffers_released(G_GNUC_UNUSED void *user)
{
  GST_FIXME ("Not sure what to do here really");
}

static void
gst_droiddec_frame_available(void *user)
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

  GST_DEBUG_OBJECT (dec, "frame available");

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (dec, "not handling frame in error state: %s",
			gst_flow_get_name (dec->downstream_flow_ret));
    goto acquire_and_release;
  }

  buff = gst_buffer_new ();

  cb.ref = gst_buffer_ref;
  cb.unref = gst_buffer_unref;
  cb.data = buff;

  mem = gst_droid_media_buffer_allocator_alloc (dec->allocator, dec->queue, &cb);

  if (!mem) {
    /* TODO: what should we do here? */
    GST_ERROR_OBJECT (dec, "failed to acquire buffer from droidmedia");
    gst_buffer_unref (buff);
    return;
  }

  buffer = gst_droid_media_buffer_memory_get_buffer (mem);

  gst_buffer_insert_memory (buff, 0, mem);

  rect = droid_media_buffer_get_crop_rect (buffer);
  crop_meta = gst_buffer_add_video_crop_meta (buff);
  crop_meta->x = rect.left;
  crop_meta->y = rect.top;
  crop_meta->width = rect.right - rect.left;
  crop_meta->height = rect.bottom - rect.top;

  width = droid_media_buffer_get_width(buffer);
  height = droid_media_buffer_get_height(buffer);

  gst_buffer_add_video_meta (buff, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_FORMAT_YV12,
			     width, height);

  frame = gst_video_decoder_get_oldest_frame (GST_VIDEO_DECODER (dec));

  if (G_UNLIKELY(!frame)) {
    /* TODO: what should we do here? */
    GST_WARNING_OBJECT (dec, "buffer without frame");
    gst_buffer_unref (buff);
    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    return;
  }

  frame->output_buffer = buff;

  /* We get the timestamp in ns already */
  frame->pts = droid_media_buffer_get_timestamp (buffer);

  flow_ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (dec), frame);

  if (flow_ret == GST_FLOW_OK || flow_ret == GST_FLOW_FLUSHING) {
    goto out;
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
  mem = gst_droid_media_buffer_allocator_alloc (dec->allocator, dec->queue, &cb);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  if (mem) {
    gst_memory_unref (mem);
  }
}

static void
gst_droiddec_signal_eos (void *data)
{
  GstDroidDec *dec = (GstDroidDec *) data;

  GST_DEBUG_OBJECT (dec, "codec signaled EOS");

  g_mutex_lock (&dec->eos_lock);

  if (!dec->eos) {
    GST_WARNING_OBJECT (dec, "codec signaled EOS but we are not expecting it");
  }

  g_cond_signal (&dec->eos_cond);
  g_mutex_unlock (&dec->eos_lock);
}

static void
gst_droiddec_error (void *data, int err)
{
  GstDroidDec *dec = (GstDroidDec *) data;

  GST_DEBUG_OBJECT (dec, "codec error");

  g_mutex_lock (&dec->eos_lock);

  if (dec->eos) {
    /* Gotta love Android. We will ignore errors if we are expecting EOS */
    g_cond_signal (&dec->eos_cond);
    goto out;
  }

  GST_VIDEO_DECODER_STREAM_LOCK (dec);
  dec->downstream_flow_ret = GST_FLOW_ERROR;
  GST_VIDEO_DECODER_STREAM_UNLOCK (dec);

  GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL, ("error 0x%x from android codec", -err));

out:
  g_mutex_unlock (&dec->eos_lock);
}

static int
gst_droiddec_size_changed(void *data, int32_t width, int32_t height)
{
  GstDroidDec *dec = (GstDroidDec *) data;
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
  int err = 0;

  GST_INFO_OBJECT (dec, "size changed: w=%d, h=%d", width, height);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (dec->out_state) {
    gst_video_codec_state_unref (dec->out_state);
  }

  dec->out_state =
    gst_droiddec_configure_state (decoder, width, height);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return err;
}

static GstVideoCodecState *
gst_droiddec_configure_state (GstVideoDecoder * decoder, gsize width,
    gsize height)
{
  GstVideoCodecState *out;
  GstCapsFeatures *feature;
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "configure state: width: %d, height: %d",
      width, height);

  out = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
      GST_VIDEO_FORMAT_YV12, width, height, dec->in_state);

  if (!out->caps) {
    /* we will add our caps */
    out->caps = gst_video_info_to_caps (&out->info);
  }

  feature = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_BUFFER, NULL);
  gst_caps_set_features (out->caps, 0, feature);

  GST_DEBUG_OBJECT (dec, "output caps %" GST_PTR_FORMAT, out->caps);

  return out;
}

static gboolean
gst_droiddec_stop (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "stop");

  if (dec->codec) {
    droid_media_codec_stop (dec->codec);
    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;
    dec->queue = NULL;
  }

  if (dec->in_state) {
    gst_video_codec_state_unref (dec->in_state);
    dec->in_state = NULL;
  }

  if (dec->out_state) {
    gst_video_codec_state_unref (dec->out_state);
    dec->out_state = NULL;
  }

  g_mutex_lock (&dec->eos_lock);
  dec->eos = FALSE;
  g_mutex_unlock (&dec->eos_lock);

  gst_buffer_replace (&dec->codec_data, NULL);

  dec->codec_type = NULL;
  dec->codec_type_data = NULL;

  return TRUE;
}

static void
gst_droiddec_finalize (GObject * object)
{
  GstDroidDec *dec = GST_DROIDDEC (object);

  GST_DEBUG_OBJECT (dec, "finalize");

  gst_droiddec_stop (GST_VIDEO_DECODER (dec));

  gst_object_unref (dec->allocator);
  dec->allocator = NULL;

  g_mutex_clear (&dec->eos_lock);
  g_cond_clear (&dec->eos_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_droiddec_open (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "open");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droiddec_close (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "close");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droiddec_start (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "start");

  dec->eos = FALSE;
  dec->downstream_flow_ret = GST_FLOW_OK;
  dec->codec_type = NULL;
  dec->codec_type_data = NULL;

  return TRUE;
}

static gboolean
gst_droiddec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  /*
   * destroying the droidmedia codec here will cause stagefright to call abort.
   * That is why we create it after we are sure that everything is correct
   */

  GST_DEBUG_OBJECT (dec, "set format %" GST_PTR_FORMAT, state->caps);

  if (dec->codec) {
    GST_FIXME_OBJECT (dec, "What to do here?");
    GST_ERROR_OBJECT (dec, "codec already configured");
    return FALSE;
  }

  dec->codec_type = gst_droid_codec_get_from_caps (state->caps, GST_DROID_CODEC_DECODER);
  if (!dec->codec_type) {
    GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, (NULL),
        ("Unknown codec type for caps %" GST_PTR_FORMAT, state->caps));
    return FALSE;
  }

  dec->in_state = gst_video_codec_state_ref (state);

  dec->out_state =
      gst_droiddec_configure_state (decoder, state->info.width,
      state->info.height);

  gst_buffer_replace (&dec->codec_data, state->codec_data);

  if (!gst_video_decoder_negotiate (decoder)) {
    GST_ERROR_OBJECT (dec, "not negotiated");
    goto free_and_out;
  }

  if (!gst_droiddec_create_codec (dec)) {
    goto free_and_out;
  }

  return TRUE;

free_and_out:
  gst_video_codec_state_unref (dec->in_state);
  gst_video_codec_state_unref (dec->out_state);

  dec->in_state = NULL;
  dec->out_state = NULL;

  return FALSE;
}

static GstFlowReturn
gst_droiddec_finish (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "finish");

  g_mutex_lock (&dec->eos_lock);
  dec->eos = TRUE;

  if (dec->codec) {
    droid_media_codec_drain (dec->codec);
  }

  /* release the lock to allow _frame_available () to do its job */
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  /* Now we wait for the codec to signal EOS */
  g_cond_wait (&dec->eos_cond, &dec->eos_lock);
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  dec->eos = FALSE;

  g_mutex_unlock (&dec->eos_lock);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_droiddec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);
  GstFlowReturn ret;
  DroidMediaCodecData data;

  GST_DEBUG_OBJECT (dec, "handle frame");

  if (!dec->codec) {
    GST_ERROR_OBJECT (dec, "codec not initialized");
    ret = GST_FLOW_ERROR;
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

  if (dec->codec_type->process_decoder_data) {
    if (!dec->codec_type->process_decoder_data (frame->input_buffer,
						dec->codec_type_data, &data.data)) {
      GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL), ("Failed to process data"));
      ret = GST_FLOW_ERROR;
      goto error;
    }
  } else {
    data.data.size = gst_buffer_get_size (frame->input_buffer);
    data.data.data = g_malloc (data.data.size);
    gst_buffer_extract (frame->input_buffer, 0, data.data.data, data.data.size);
  }

  /* This can deadlock if droidmedia/stagefright input buffer queue is full thus we
   * cannot write the input buffer. We end up waiting for the write operation
   * which does not happen because stagefright needs us to provide
   * output buffers to be filled (which can not happen because _loop() tries
   * to call get_oldest_frame() which acquires the stream lock the base class
   * is holding before calling us
   */

  data.ts = GST_TIME_AS_USECONDS(frame->dts);
  data.sync = GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame) ? true : false;

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  if (!gst_droid_codec_consume_frame2 (dec->codec, frame, &data)) {
    GST_VIDEO_DECODER_STREAM_LOCK (decoder);
    ret = GST_FLOW_ERROR;
    goto error;
  }
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

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

out:
  ret = GST_FLOW_OK;

  return ret;

error:
  /* don't leak the frame */
  gst_video_decoder_release_frame (decoder, frame);

  return ret;
}

static gboolean
gst_droiddec_flush (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "flush");

  g_mutex_lock (&dec->eos_lock);
  dec->eos = FALSE;
  g_mutex_unlock (&dec->eos_lock);

  if (dec->codec) {
    droid_media_codec_flush (dec->codec);
  }

  dec->downstream_flow_ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (dec, "Flushed");

  return TRUE;
}

static void
gst_droiddec_init (GstDroidDec * dec)
{
  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (dec), TRUE);

  dec->codec = NULL;
  dec->queue = NULL;
  dec->codec_type = NULL;
  dec->downstream_flow_ret = GST_FLOW_OK;
  dec->eos = FALSE;
  dec->codec_data = NULL;

  g_mutex_init (&dec->eos_lock);
  g_cond_init (&dec->eos_cond);

  dec->allocator = gst_droid_media_buffer_allocator_new ();
  dec->in_state = NULL;
  dec->out_state = NULL;
}

static void
gst_droiddec_class_init (GstDroidDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideoDecoderClass *gstvideodecoder_class;
  GstCaps *caps;
  GstPadTemplate *tpl;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstvideodecoder_class = (GstVideoDecoderClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Video decoder", "Decoder/Video/Device",
      "Android HAL decoder", "Mohammed Sameer <msameer@foolab.org>");

  caps = gst_droid_codec_get_all_caps (GST_DROID_CODEC_DECODER);
  tpl = gst_pad_template_new (GST_VIDEO_DECODER_SINK_NAME,
      GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (gstelement_class, tpl);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droiddec_src_template_factory));

  gobject_class->finalize = gst_droiddec_finalize;

  gstvideodecoder_class->open = GST_DEBUG_FUNCPTR (gst_droiddec_open);
  gstvideodecoder_class->close = GST_DEBUG_FUNCPTR (gst_droiddec_close);
  gstvideodecoder_class->start = GST_DEBUG_FUNCPTR (gst_droiddec_start);
  gstvideodecoder_class->stop = GST_DEBUG_FUNCPTR (gst_droiddec_stop);
  gstvideodecoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_droiddec_set_format);
  gstvideodecoder_class->finish = GST_DEBUG_FUNCPTR (gst_droiddec_finish);
  gstvideodecoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_droiddec_handle_frame);
  gstvideodecoder_class->flush = GST_DEBUG_FUNCPTR (gst_droiddec_flush);
}
