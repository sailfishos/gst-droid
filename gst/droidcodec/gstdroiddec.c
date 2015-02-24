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
#include "gst/memory/gstdroidmediabuffer.h"
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

typedef struct
{
  GstDroidDec *dec;
  GstBuffer *buffer;
  GstMapInfo info;
  GstVideoCodecFrame *frame;
} GstDroidDecReleaseBufferInfo;

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
  GstFlowReturn flow_ret;

  GST_DEBUG_OBJECT (dec, "frame available");

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  /* TODO: we are still missing a lot of checks here */
  mem = gst_droid_media_buffer_allocator_alloc (dec->allocator, dec->queue);

  if (!mem) {
    /* TODO: do we want an error here? */
    GST_ERROR_OBJECT (dec, "failed to acquire buffer from droidmedia");
    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    return;
  }

  buffer = gst_droid_media_buffer_memory_get_buffer (mem);

  buff = gst_buffer_new ();

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
    GST_WARNING_OBJECT (dec, "buffer without frame");
    gst_buffer_unref (buff);
    /* TODO: do we want an error here? */
    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    return;
  }

  frame->output_buffer = buff;

  /* We get the timestamp in ns already */
  frame->pts = droid_media_buffer_get_timestamp (buffer);

  flow_ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (dec), frame);

  if (flow_ret != GST_FLOW_OK) {
    /* TODO: handle that */
  }

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
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

  g_mutex_lock (&dec->running_lock);
  if (!dec->running) {
    GST_INFO_OBJECT (dec, "error 0x%x ignored because we are not running", -err);
    g_mutex_unlock (&dec->running_lock);
    goto out;
  }

  g_mutex_unlock (&dec->running_lock);

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
    g_mutex_lock (&dec->running_lock);
    dec->running = FALSE;
    g_mutex_unlock (&dec->running_lock);

    /* call stop first so it unlocks the loop */
    droid_media_codec_stop (dec->codec);
    gst_pad_stop_task (GST_VIDEO_DECODER_SRC_PAD (decoder));
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

  g_mutex_clear (&dec->running_lock);

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

  dec->codec_type = gst_droid_codec_get_from_caps (state->caps, GST_DROID_CODEC_DECODER);
  if (!dec->codec_type) {
    GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, (NULL),
        ("Unknown codec type for caps %" GST_PTR_FORMAT, state->caps));
    return FALSE;
  }

  md.parent.type = dec->codec_type->droid;
  md.parent.width = state->info.width;
  md.parent.height = state->info.height;
  md.parent.fps = state->info.fps_n / state->info.fps_d;
  md.parent.flags = DROID_MEDIA_CODEC_HW_ONLY;
  md.codec_data.size = 0;

  dec->in_state = gst_video_codec_state_ref (state);

  dec->out_state =
      gst_droiddec_configure_state (decoder, state->info.width,
      state->info.height);

  if (state->codec_data) {
    g_assert (dec->codec_type->construct_decoder_codec_data);

    if (!dec->codec_type->construct_decoder_codec_data (state->codec_data, &md.codec_data)) {
      GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
			 ("Failed to construct codec_data."));

      gst_video_codec_state_unref (dec->in_state);
      gst_video_codec_state_unref (dec->out_state);

      dec->in_state = NULL;
      dec->out_state = NULL;

      return FALSE;
    }
  }

  dec->codec = droid_media_codec_create_decoder(&md);

  if (md.codec_data.size > 0) {
    g_free (md.codec_data.data);
  }

  if (!dec->codec) {
    GST_ELEMENT_ERROR(dec, LIBRARY, SETTINGS, NULL, ("Failed to create decoder"));

    gst_video_codec_state_unref (dec->in_state);
    gst_video_codec_state_unref (dec->out_state);

    dec->in_state = NULL;
    dec->out_state = NULL;

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

static void
gst_droiddec_release_buffer (void *data)
{
  GstDroidDecReleaseBufferInfo *info = (GstDroidDecReleaseBufferInfo *) data;

  GST_DEBUG_OBJECT (info->dec, "release buffer %p", info->buffer);
  gst_buffer_unmap (info->buffer, &info->info);
  gst_buffer_unref (info->buffer);

  /* We need to release the input buffer */
  gst_buffer_unref (info->frame->input_buffer);
  info->frame->input_buffer = NULL;

  gst_video_codec_frame_unref (info->frame);
  g_slice_free(GstDroidDecReleaseBufferInfo, info);
}

static void
gst_droiddec_loop (gpointer data)
{
  GstDroidDec *dec = (GstDroidDec *) data;

  GST_LOG_OBJECT (dec, "loop");

  g_mutex_lock (&dec->running_lock);

  if (!dec->running) {
    g_mutex_unlock (&dec->running_lock);
    goto stop_and_out;
  }

  g_mutex_unlock (&dec->running_lock);

  if (droid_media_codec_loop (dec->codec)) {
    return;
  }

  /* TODO: we need a better return value for _loop because false does not always mean an error */
  GST_ERROR_OBJECT (dec, "decoder loop returned error");

stop_and_out:
  g_mutex_lock (&dec->running_lock);
  dec->running = FALSE;
  g_mutex_unlock (&dec->running_lock);

  gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (GST_VIDEO_DECODER(dec)));
  GST_DEBUG_OBJECT (dec, "loop exit");
}

static GstFlowReturn
gst_droiddec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);
  GstFlowReturn ret;
  DroidMediaCodecData *data = NULL;
  GstMapInfo info;
  DroidMediaBufferCallbacks cb;
  GstDroidDecReleaseBufferInfo *release_data;

  GST_DEBUG_OBJECT (dec, "handle frame");

  if (!dec->codec) {
    GST_ERROR_OBJECT (dec, "codec not initialized");
    ret = GST_FLOW_ERROR;
    goto out;
  }

  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_INFO_OBJECT (dec, "not handling frame in error state");
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

  /* This can deadlock if droidmedia/stagefright input buffer queue is full thus we
   * cannot write the input buffer. We end up waiting for the write operation
   * which does not happen because stagefright needs us to provide
   * output buffers to be filled (which can not happen because _loop() tries
   * to call get_oldest_frame() which acquires the stream lock the base class
   * is holding before calling us
   */
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  data = droid_media_codec_dequeue_input_buffer (dec->codec);
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_INFO_OBJECT (dec, "not handling frame in error state");
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

  if (G_UNLIKELY(!data)) {
    /* with all the above checks, this is impossible to happen but you never know */
    GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL, ("failed to dequeue input buffer from the decoder"));
    ret = GST_FLOW_ERROR;
    goto out;
  }

  data->sync = GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame) ? true : false;
  data->ts = GST_TIME_AS_USECONDS(frame->pts);

  if (!gst_buffer_map (frame->input_buffer, &info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    ret = GST_FLOW_ERROR;
    goto out;
  }

  data->data.size = info.size;
  data->data.data = info.data;

  GST_LOG_OBJECT (dec, "handling frame data of size %d", data->data.size);

  release_data = g_slice_new (GstDroidDecReleaseBufferInfo);

  cb.unref = gst_droiddec_release_buffer;
  cb.data = release_data;
  release_data->dec = dec;
  release_data->buffer = gst_buffer_ref (frame->input_buffer);
  release_data->info = info;
  release_data->frame = frame; /* We have a ref already */
  droid_media_codec_queue_input_buffer (dec->codec, data, &cb);
  ret = GST_FLOW_OK;
  data = NULL;

  g_mutex_lock (&dec->running_lock);

  if (!dec->running) {
    dec->running = TRUE;
    gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (decoder), gst_droiddec_loop, dec, NULL);
  }

  g_mutex_unlock (&dec->running_lock);

out:
  if (ret != GST_FLOW_OK) {
    /* don't leak the frame */
    gst_video_decoder_release_frame (decoder, frame);
  }

  if (data) {
    droid_media_codec_release_input_buffer (dec->codec, data);
  }

  return ret;
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
  dec->queue = NULL;
  dec->codec_type = NULL;
  dec->eos = FALSE;
  dec->downstream_flow_ret = GST_FLOW_OK;

  g_mutex_init (&dec->eos_lock);
  g_cond_init (&dec->eos_cond);

  g_mutex_init (&dec->running_lock);

  dec->allocator = gst_droid_media_buffer_allocator_new ();
  dec->in_state = NULL;
  dec->out_state = NULL;
}

static GstStateChangeReturn
gst_droiddec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDroidDec *dec;
  GstVideoDecoder *decoder;

  decoder = GST_VIDEO_DECODER (element);
  dec = GST_DROIDDEC (element);

  GST_DEBUG_OBJECT (dec, "change state");

  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY) {
    GST_VIDEO_DECODER_STREAM_LOCK (decoder);
    gst_droiddec_stop (decoder);
    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

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

  caps = gst_droid_codec_get_all_caps (GST_DROID_CODEC_DECODER);
  tpl = gst_pad_template_new (GST_VIDEO_DECODER_SINK_NAME,
      GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (gstelement_class, tpl);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droiddec_src_template_factory));

  gobject_class->finalize = gst_droiddec_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_droiddec_change_state);
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
