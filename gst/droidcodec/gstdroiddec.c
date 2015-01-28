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

#include "gstdroiddec.h"
#include "gst/memory/gstgralloc.h"
#include "gstdroidcodectype.h"
#include "plugin.h"
#include <gst/memory/gstwrappedmemory.h>
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
        (GST_CAPS_FEATURE_MEMORY_DROID_HANDLE, "{ENCODED, YV12}")));

static void
gst_droiddec_release_frame (DroidMediaBuffer *buffer)
{
  GST_DEBUG ("release frame");

  droid_media_buffer_release (buffer, EGL_NO_DISPLAY, EGL_NO_SYNC_KHR);
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
  DroidMediaBuffer *buffer;
  DroidMediaBufferCallbacks cb;
  GstMemory *mem;
  guint width, height;
  GstVideoCodecFrame *frame;

  GST_DEBUG_OBJECT (dec, "frame available");

  /* TODO: size */
  mem = gst_wrapped_memory_allocator_memory_new (dec->allocator, 0);

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
}

static void
gst_droiddec_signal_eos (void *data)
{
  GstDroidDec *dec = (GstDroidDec *) data;

  GST_DEBUG_OBJECT (dec, "codec signaled EOS");

  /* TODO: Is it possible that we get EOS when we are not expecting it */
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
  dec->has_error = TRUE;
  GST_VIDEO_DECODER_STREAM_UNLOCK (dec);

  GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL, ("error 0x%x from android codec", -err));

out:
  g_mutex_unlock (&dec->eos_lock);
}

static gboolean
gst_droiddec_do_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "do handle frame");

  /* This can deadlock if droidmedia/stagefright input buffer queue is full thus we
   * cannot write the input buffer. We end up waiting for the write operation
   * which does not happen because stagefright needs us to provide
   * output buffers to be filled (which can not happen because _loop() tries
   * to call get_oldest_frame() which acquires the stream lock the base class
   * is holding before calling us
   */
  // TODO: Check that we are still in a sane state
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  if (!gst_droid_codec_consume_frame (dec->codec, frame, frame->dts)) {
    GST_VIDEO_DECODER_STREAM_LOCK (decoder);
    // TODO: error
    return FALSE;
  }

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  return TRUE;
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
      GST_VIDEO_FORMAT_ENCODED, width, height, dec->in_state);

  if (!out->caps) {
    /* we will add our caps */
    out->caps = gst_video_info_to_caps (&out->info);
  }

  feature = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DROID_HANDLE, NULL);
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
  }

  if (dec->in_state) {
    gst_video_codec_state_unref (dec->in_state);
    dec->in_state = NULL;
  }

  if (dec->out_state) {
    gst_video_codec_state_unref (dec->out_state);
    dec->out_state = NULL;
  }

  dec->eos = FALSE;

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
  dec->has_error = FALSE;

  return TRUE;
}

static gboolean
gst_droiddec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  DroidMediaCodecDecoderMetaData md;

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

  md.parent.type = gst_droid_codec_type_from_caps (state->caps, GST_DROID_CODEC_DECODER);
  if (!md.parent.type) {
    GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, (NULL),
        ("Unknown codec type for caps %" GST_PTR_FORMAT, state->caps));
    return FALSE;
  }

  md.parent.width = state->info.width;
  md.parent.height = state->info.height;
  md.parent.fps = state->info.fps_n / state->info.fps_d; // TODO: bad
  md.parent.flags = DROID_MEDIA_CODEC_HW_ONLY;
  md.codec_data_size = 0;

  if (state->codec_data) {
    md.codec_data_size = gst_buffer_get_size (state->codec_data);
    md.codec_data = g_malloc(md.codec_data_size);

    // TODO: error checking
    gst_buffer_extract (state->codec_data, 0, md.codec_data, md.codec_data_size);
  }

  dec->in_state = gst_video_codec_state_ref (state);

  dec->out_state =
      gst_droiddec_configure_state (decoder, state->info.width,
      state->info.height);

  if (!gst_video_decoder_negotiate (decoder)) {
    // TODO: error

    gst_video_codec_state_unref (dec->in_state);
    gst_video_codec_state_unref (dec->out_state);

    dec->in_state = NULL;
    dec->out_state = NULL;

    return FALSE;
  }

  dec->codec = droid_media_codec_create_decoder(&md);

  if (md.codec_data_size > 0) {
    g_free (md.codec_data);
  }

  if (!dec->codec) {
    GST_ELEMENT_ERROR(dec, LIBRARY, SETTINGS, NULL, ("Failed to create decoder"));

    gst_video_codec_state_unref (dec->in_state);
    gst_video_codec_state_unref (dec->out_state);

    dec->in_state = NULL;
    dec->out_state = NULL;

    return FALSE;
  }

  {
    DroidMediaCodecCallbacks cb;
    cb.signal_eos = gst_droiddec_signal_eos;
    cb.error = gst_droiddec_error;
    droid_media_codec_set_callbacks (dec->codec, &cb, dec);
  }

  {
    DroidMediaRenderingCallbacks cb;
    cb.buffers_released = gst_droiddec_buffers_released;
    cb.frame_available = gst_droiddec_frame_available;
    droid_media_codec_set_rendering_callbacks (dec->codec, &cb, dec);
  }

  if (!droid_media_codec_start (dec->codec)) {
    // TODO: error
    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;

    gst_video_codec_state_unref (dec->in_state);
    gst_video_codec_state_unref (dec->out_state);

    dec->in_state = NULL;
    dec->out_state = NULL;

    return FALSE;
  }

  return TRUE;
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

  g_mutex_unlock (&dec->eos_lock);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_droiddec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "handle frame");

  if (!dec->codec) {
    GST_ERROR_OBJECT (dec, "codec not initialized");
    goto error;
  }

  if (dec->has_error) {
    GST_INFO_OBJECT (dec, "not handling frame in error state");
    goto error;
  }

  if (gst_droiddec_do_handle_frame (decoder, frame)) {
    return GST_FLOW_OK;
  }

#if 0
  /* if we have been flushed then we need to start accepting data again */
  if (!gst_droid_codec_is_running (dec->comp)) {
    if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame)) {
      GST_WARNING_OBJECT (dec, "dropping non sync frame");
      gst_video_decoder_drop_frame (decoder, frame);
      return GST_FLOW_OK;
    }

    if (!gst_droid_codec_flush (dec->comp, FALSE)) {
      goto error;
    }

    gst_droid_codec_empty_full (dec->comp);

    if (!gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (decoder),
            (GstTaskFunction) gst_droiddec_loop, gst_object_ref (dec),
            gst_object_unref)) {
      GST_ERROR_OBJECT (dec, "failed to start src task");
      goto error;
    }
  }

  if (gst_droiddec_do_handle_frame (decoder, frame)) {
    return GST_FLOW_OK;
  }

  if (!gst_droid_codec_is_running (dec->comp)) {
    /* don't leak the frame */
    gst_video_decoder_release_frame (decoder, frame);
    return GST_FLOW_FLUSHING;
  }

  if (gst_droid_codec_needs_reconfigure (dec->comp)) {
    gst_droiddec_stop_loop (decoder);

    /* reconfigure */
    if (!gst_droid_codec_reconfigure_output_port (dec->comp)) {
      /* failed */
      goto error;
    }
  }

  gst_droid_codec_unset_needs_reconfigure (dec->comp);

  /* update codec state and src caps */
  if (dec->out_state) {
    gst_video_codec_state_unref (dec->out_state);
  }

  width = dec->comp->out_port->def.format.video.nFrameWidth;
  height = dec->comp->out_port->def.format.video.nFrameHeight;
  hal_fmt = dec->comp->out_port->def.format.video.eColorFormat;
  dec->out_state = gst_droiddec_configure_state (decoder,
      width, height, hal_fmt);

  /* now the buffer pool */
  config = gst_buffer_pool_get_config (dec->comp->out_port->buffers);
  gst_buffer_pool_config_set_params (config, dec->out_state->caps,
      dec->comp->out_port->def.nBufferSize,
      dec->comp->out_port->def.nBufferCountActual,
      dec->comp->out_port->def.nBufferCountActual);
  gst_buffer_pool_config_set_allocator (config, dec->comp->out_port->allocator,
      NULL);

  if (!gst_buffer_pool_set_config (dec->comp->out_port->buffers, config)) {
    GST_ERROR_OBJECT (dec, "failed to set buffer pool configuration");
    goto error;
  }

  if (!gst_video_decoder_negotiate (decoder)) {
    goto error;
  }

  if (!gst_buffer_pool_set_active (dec->comp->out_port->buffers, TRUE)) {
    GST_ERROR_OBJECT (dec, "failed to activate buffer pool");
    goto error;
  }

  /* start the loop */
  gst_droid_codec_set_running (dec->comp, TRUE);

  if (!gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (decoder),
          (GstTaskFunction) gst_droiddec_loop, gst_object_ref (dec),
          gst_object_unref)) {
    GST_ERROR_OBJECT (dec, "failed to start src task");
    goto error;
  }

  if (gst_droiddec_do_handle_frame (decoder, frame)) {
    return GST_FLOW_OK;
  }

#endif

  //  return GST_FLOW_OK;

error:
  /* don't leak the frame */
  gst_video_decoder_release_frame (decoder, frame);

  return GST_FLOW_ERROR;
}

static gboolean
gst_droiddec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "decide allocation %" GST_PTR_FORMAT, query);

  {
    // TODO: Cleanup this
    GstBufferPool *pool = gst_buffer_pool_new ();
    GstStructure *s = gst_buffer_pool_get_config (pool);
    GstCaps *caps = gst_caps_new_simple ("video/x-raw",
					 "width", G_TYPE_INT, 10,
					 "height", G_TYPE_INT, 10,
					 "format", G_TYPE_STRING, gst_video_format_to_string (GST_VIDEO_FORMAT_I420),
					 NULL);
    gst_buffer_pool_config_set_allocator (s, gst_allocator_find (GST_ALLOCATOR_SYSMEM), NULL);
    gst_buffer_pool_config_set_params (s, caps, 10, 1, 1);
    gst_buffer_pool_set_config (pool, s);

    if (gst_query_get_n_allocation_pools (query) > 0) {
      gst_query_set_nth_allocation_pool (query, 0, pool, 10, 10, 10);
  } else {
      gst_query_add_allocation_pool (query, pool, 10, 10, 10);
  }
  }

#if 0
  conf = gst_buffer_pool_get_config (dec->comp->out_port->buffers);

  if (!gst_buffer_pool_config_get_params (conf, NULL, &size, NULL, NULL)) {
    GST_ERROR_OBJECT (dec, "failed to get buffer pool configuration");
    gst_structure_free (conf);
    return FALSE;
  }

  gst_structure_free (conf);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_set_nth_allocation_pool (query, 0, dec->comp->out_port->buffers,
        size, size, size);
  } else {
    gst_query_add_allocation_pool (query, dec->comp->out_port->buffers, size,
        size, size);
  }
#endif
  return TRUE;
}

static gboolean
gst_droiddec_flush (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "flush");

  dec->eos = FALSE;

  if (dec->codec) {
    droid_media_codec_flush (dec->codec);
  }

  GST_DEBUG_OBJECT (dec, "Flushed");

  return TRUE;
}

static void
gst_droiddec_init (GstDroidDec * dec)
{
  dec->codec = NULL;
  dec->eos = FALSE;
  dec->has_error = FALSE;

  g_mutex_init (&dec->eos_lock);
  g_cond_init (&dec->eos_cond);

  dec->allocator = gst_wrapped_memory_allocator_new ();
  dec->in_state = NULL;
  dec->out_state = NULL;
}

static gboolean
gst_droiddec_negotiate (GstVideoDecoder * decoder)
{
  GstDroidDec *dec;
  GstPad *pad;
  GstCaps *caps = NULL;
  gboolean ret = FALSE;

  /* TODO: something is wrong here.
   * If I don't implement a _negotiate function then I never get an error from downstream
   * elements.
   * If I don't query the peer caps then I don't get an error but downstream does not
   * like the caps.
   */
  dec = GST_DROIDDEC (decoder);
  pad = GST_VIDEO_DECODER_SRC_PAD (decoder);

  GST_DEBUG_OBJECT (dec, "negotiate with caps %" GST_PTR_FORMAT,
      dec->out_state->caps);

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder)) {
    return FALSE;
  }

  GST_DEBUG_OBJECT (dec, "peer caps %"GST_PTR_FORMAT, gst_pad_peer_query_caps (pad, NULL));
  /* We don't negotiate. We either use our caps or fail */
  caps = gst_pad_peer_query_caps (pad, dec->out_state->caps);

  GST_DEBUG_OBJECT (dec, "intersection %" GST_PTR_FORMAT, caps);

  if (gst_caps_is_empty (caps)) {
    goto error;
  }

  if (!gst_caps_is_equal (caps, dec->out_state->caps)) {
    goto error;
  }

  if (!gst_pad_set_caps (pad, caps)) {
    goto error;
  }

  ret = TRUE;
  goto out;

error:
  GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
      ("failed to negotiate output format"));

out:
  if (caps) {
    gst_caps_unref (caps);
  }

  return ret;
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

  caps = gst_droid_codec_type_all_caps (GST_DROID_CODEC_DECODER);
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
  gstvideodecoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_droiddec_decide_allocation);
  gstvideodecoder_class->flush = GST_DEBUG_FUNCPTR (gst_droiddec_flush);
  gstvideodecoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_droiddec_negotiate);
}
