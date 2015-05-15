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
#include "gst/droid/gstdroidbufferpool.h"
#include "plugin.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <string.h>             /* memset() */
#ifdef HAVE_ORC
#include <orc/orc.h>
#else
#define orc_memcpy memcpy
#endif

#define GST_DROID_DEC_NUM_BUFFERS         2

#define gst_droiddec_parent_class parent_class
G_DEFINE_TYPE (GstDroidDec, gst_droiddec, GST_TYPE_VIDEO_DECODER);

GST_DEBUG_CATEGORY_EXTERN (gst_droid_vdec_debug);
#define GST_CAT_DEFAULT gst_droid_vdec_debug

static GstStaticPadTemplate gst_droiddec_src_template_factory =
    GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_BUFFER, "{YV12}") ";"
        GST_VIDEO_CAPS_MAKE ("I420")));

static gboolean gst_droiddec_configure_state (GstVideoDecoder * decoder);
static void gst_droiddec_error (void *data, int err);
static int gst_droiddec_size_changed (void *data, int32_t width,
    int32_t height);
static void gst_droiddec_signal_eos (void *data);
static void gst_droiddec_buffers_released (void *user);
static void gst_droiddec_frame_available (void *user);
static void gst_droiddec_data_available (void *data,
    DroidMediaCodecData * encoded);
static gboolean gst_droiddec_convert_buffer (GstDroidDec * dec, GstBuffer * out,
    DroidMediaData * in, GstVideoInfo * info);
static void gst_droiddec_loop (GstDroidDec * dec);
static GstFlowReturn gst_droiddec_finish_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

static void
gst_droiddec_loop (GstDroidDec * dec)
{
  GST_LOG_OBJECT (dec, "loop");

  if (!gst_droid_buffer_pool_wait_for_buffer (dec->pool)) {
    goto out;
  }

  while (droid_media_codec_loop (dec->codec)
      == DROID_MEDIA_CODEC_LOOP_OK) {
    GST_LOG_OBJECT (dec, "tick");
    return;
  }

out:
  GST_INFO_OBJECT (dec, "pausing task");
  gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (GST_VIDEO_DECODER (dec)));
}

static gboolean
gst_droiddec_create_codec (GstDroidDec * dec)
{
  DroidMediaCodecDecoderMetaData md;
  const gchar *droid = gst_droid_codec_get_droid_type (dec->codec_type);

  GST_INFO_OBJECT (dec, "create codec of type %s: %dx%d",
      droid, dec->in_state->info.width, dec->in_state->info.height);

  memset (&md, 0x0, sizeof (md));

  /* Let's take care of the buffer pool first */
  if (!dec->pool) {
    GstStructure *config;

    dec->pool = gst_droid_buffer_pool_new ();
    config = gst_buffer_pool_get_config (dec->pool);
    /* pass NULL for the caps. We don't have it yet */
    gst_buffer_pool_config_set_params (config, NULL, 0,
        GST_DROID_DEC_NUM_BUFFERS, GST_DROID_DEC_NUM_BUFFERS);

    if (!gst_buffer_pool_set_config (dec->pool, config)) {
      GST_ELEMENT_ERROR (dec, STREAM, FAILED, (NULL),
          ("Failed to configure buffer pool"));
      return FALSE;
    }

    if (!gst_buffer_pool_set_active (dec->pool, TRUE)) {
      GST_ELEMENT_ERROR (dec, STREAM, FAILED, (NULL),
          ("Failed to activate buffer pool"));
      return FALSE;
    }
  } else {
    gst_buffer_pool_set_flushing (dec->pool, FALSE);
  }

  md.parent.type = droid;
  md.parent.width = dec->in_state->info.width;
  md.parent.height = dec->in_state->info.height;
  md.parent.fps = dec->in_state->info.fps_n / dec->in_state->info.fps_d;
  md.parent.flags =
      DROID_MEDIA_CODEC_HW_ONLY | DROID_MEDIA_CODEC_USE_EXTERNAL_LOOP;
  md.codec_data.size = 0;

  if (dec->info.finfo->format == GST_VIDEO_FORMAT_I420) {
    md.parent.flags |= DROID_MEDIA_CODEC_NO_MEDIA_BUFFER;
  }

  /* TODO: get rid of NULL */
  switch (gst_droid_codec_create_decoder_codec_data (dec->codec_type,
          dec->codec_data, &md.codec_data, NULL)) {
    case GST_DROID_CODEC_CODEC_DATA_OK:
      break;

    case GST_DROID_CODEC_CODEC_DATA_NOT_NEEDED:
      g_assert (dec->codec_data == NULL);
      break;

    case GST_DROID_CODEC_CODEC_DATA_ERROR:
      GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
          ("Failed to create codec_data."));
      goto error;
  }

  dec->codec = droid_media_codec_create_decoder (&md);

  if (md.codec_data.size > 0) {
    g_free (md.codec_data.data);
  }

  if (!dec->codec) {
    GST_ELEMENT_ERROR (dec, LIBRARY, SETTINGS, NULL,
        ("Failed to create decoder"));

    goto error;
  }

  dec->queue = droid_media_codec_get_buffer_queue (dec->codec);

  {
    DroidMediaCodecCallbacks cb;
    cb.signal_eos = gst_droiddec_signal_eos;
    cb.error = gst_droiddec_error;
    cb.size_changed = gst_droiddec_size_changed;
    droid_media_codec_set_callbacks (dec->codec, &cb, dec);
  }

  if (dec->queue) {
    DroidMediaBufferQueueCallbacks cb;
    cb.buffers_released = gst_droiddec_buffers_released;
    cb.frame_available = gst_droiddec_frame_available;
    droid_media_buffer_queue_set_callbacks (dec->queue, &cb, dec);
  } else {
    DroidMediaCodecDataCallbacks cb;
    cb.data_available = gst_droiddec_data_available;
    droid_media_codec_set_data_callbacks (dec->codec, &cb, dec);
  }

  if (!droid_media_codec_start (dec->codec)) {
    GST_ELEMENT_ERROR (dec, LIBRARY, INIT, (NULL),
        ("Failed to start the decoder"));

    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;
    dec->queue = NULL;

    goto error;
  }

  /* now start our task */
  gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (GST_VIDEO_DECODER (dec)),
      (GstTaskFunction) gst_droiddec_loop, gst_object_ref (dec),
      gst_object_unref);
  return TRUE;

error:
  gst_buffer_pool_set_active (dec->pool, FALSE);
  return FALSE;
}

static void
gst_droiddec_buffers_released (G_GNUC_UNUSED void *user)
{
  GST_FIXME ("Not sure what to do here really");
}

static gboolean
gst_droiddec_convert_buffer (GstDroidDec * dec,
    GstBuffer * out, DroidMediaData * in, GstVideoInfo * info)
{
  gboolean use_external_buffer =
      GST_VIDEO_INFO_COMP_STRIDE (info, 0) != info->width;
  guint8 *data = NULL;
  gboolean ret;
  GstMapInfo map_info;

  GST_DEBUG_OBJECT (dec, "convert buffer");

  map_info.data = NULL;

  if (!gst_buffer_map (out, &map_info, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (dec, "failed to map buffer");
    ret = FALSE;
    goto out;
  }

  if (use_external_buffer) {
    GST_DEBUG_OBJECT (dec, "using an external buffer for I420 conversion.");
    data = g_malloc (info->width * info->height * 3 / 2);
  } else {
    data = map_info.data;
  }

  if (droid_media_convert_to_i420 (dec->convert, in, data) != true) {
    GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, (NULL),
        ("failed to convert frame"));

    ret = FALSE;
    goto out;
  }

  if (use_external_buffer) {
    /* fix up the buffer */
    /* Code is based on gst-colorconv qcom backend */

    gint stride = GST_VIDEO_INFO_COMP_STRIDE (info, 0);
    gint strideUV = GST_VIDEO_INFO_COMP_STRIDE (info, 1);
    guint8 *p = data;
    guint8 *dst = map_info.data;
    int i;
    int x;

    /* Y */
    for (i = 0; i < info->height; i++) {
      orc_memcpy (dst, p, info->width);
      dst += stride;
      p += info->width;
    }

    /* U and V */
    for (x = 0; x < 2; x++) {
      for (i = 0; i < info->height / 2; i++) {
        orc_memcpy (dst, p, info->width / 2);
        dst += strideUV;
        p += info->width / 2;
      }
    }
  }

  ret = TRUE;

out:
  if (use_external_buffer && data) {
    g_free (data);
  }

  if (map_info.data) {
    gst_buffer_unmap (out, &map_info);
  }

  return ret;
}

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

  if (G_UNLIKELY (!dec->out_state)) {
    if (!gst_droiddec_configure_state (decoder)) {
      flow_ret = GST_FLOW_ERROR;
      goto acquire_and_release;
    }
  }

  flow_ret = gst_buffer_pool_acquire_buffer (dec->pool, &buff, NULL);

  if (flow_ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (dec, "failed to acquire buffer from pool: %s",
        gst_flow_get_name (flow_ret));
    goto acquire_and_release;
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

  gst_buffer_insert_memory (buff, 0, mem);

  /* we don't want to access the memory afterwards */
  mem = NULL;
  buffer = NULL;

  crop_meta = gst_buffer_add_video_crop_meta (buff);
  crop_meta->x = rect.left;
  crop_meta->y = rect.top;
  crop_meta->width = rect.right - rect.left;
  crop_meta->height = rect.bottom - rect.top;

  GST_LOG_OBJECT (dec, "crop info: x=%d, y=%d, w=%d, h=%d", crop_meta->x,
      crop_meta->y, crop_meta->width, crop_meta->height);

  gst_buffer_add_video_meta (buff, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_YV12, width, height);

  frame = gst_video_decoder_get_oldest_frame (decoder);

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

  dec->downstream_flow_ret = gst_droiddec_finish_frame (decoder, frame);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  return;

acquire_and_release:
  /* we can not use our cb struct here so ask droidmedia to do
   * the work instead */
  droid_media_buffer_queue_acquire_and_release (dec->queue);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
}

static void
gst_droiddec_data_available (void *data, DroidMediaCodecData * encoded)
{
  GstDroidDec *dec = (GstDroidDec *) data;
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
  GstBuffer *buff;
  GstFlowReturn flow_ret;
  GstVideoCodecFrame *frame;

  GST_DEBUG_OBJECT (dec, "data available");

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (dec->dirty) {
    flow_ret = dec->downstream_flow_ret;
    goto out;
  }

  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (dec, "not handling frame in error state: %s",
        gst_flow_get_name (dec->downstream_flow_ret));
    flow_ret = dec->downstream_flow_ret;
    goto out;
  }

  if (G_UNLIKELY (!dec->out_state)) {
    if (!gst_droiddec_configure_state (decoder)) {
      flow_ret = GST_FLOW_ERROR;
      goto out;
    }
  }

  buff = gst_video_decoder_allocate_output_buffer (decoder);

  gst_buffer_add_video_meta (buff, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_I420, dec->out_state->info.width,
      dec->out_state->info.height);

  if (!gst_droiddec_convert_buffer (dec, buff, &encoded->data,
          &dec->out_state->info)) {
    gst_buffer_unref (buff);
    flow_ret = GST_FLOW_ERROR;
    goto out;
  }

  frame = gst_video_decoder_get_oldest_frame (decoder);

  if (G_UNLIKELY (!frame)) {
    /* TODO: what should we do here? */
    GST_WARNING_OBJECT (dec, "buffer without frame");
    gst_buffer_unref (buff);
    flow_ret = dec->downstream_flow_ret;
    goto out;
  }

  /* We get the timestamp in ns already */
  frame->pts = encoded->ts;
  frame->output_buffer = buff;

  /* we have a ref so drop it */
  gst_video_codec_frame_unref (frame);

  flow_ret = gst_droiddec_finish_frame (decoder, frame);

out:
  dec->downstream_flow_ret = flow_ret;
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  return;
}

static GstFlowReturn
gst_droiddec_finish_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstFlowReturn flow_ret;

  GST_DEBUG_OBJECT (decoder, "finish frame");

  flow_ret = gst_video_decoder_finish_frame (decoder, frame);

  if (flow_ret == GST_FLOW_OK || flow_ret == GST_FLOW_FLUSHING) {
    goto out;
  } else if (flow_ret == GST_FLOW_EOS) {
    GST_INFO_OBJECT (decoder, "eos");
  } else if (flow_ret < GST_FLOW_OK) {
    GST_ELEMENT_ERROR (decoder, STREAM, FAILED,
        ("Internal data stream error."), ("stream stopped, reason %s",
            gst_flow_get_name (flow_ret)));
  }

out:
  return flow_ret;
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

  if (dec->pool) {
    gst_buffer_pool_set_flushing (dec->pool, TRUE);
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

  GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL,
      ("error 0x%x from android codec", -err));

out:
  if (dec->pool) {
    gst_buffer_pool_set_flushing (dec->pool, TRUE);
  }

  g_mutex_unlock (&dec->eos_lock);
}

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
    dec->out_state = NULL;
  }

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return err;
}

static gboolean
gst_droiddec_configure_state (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);
  DroidMediaCodecMetaData md;
  DroidMediaRect rect;
  gsize width, height;

  memset (&md, 0x0, sizeof (md));
  memset (&rect, 0x0, sizeof (rect));

  droid_media_codec_get_output_info (dec->codec, &md, &rect);

  GST_DEBUG_OBJECT (dec,
      "configure state: width: %d, height: %d, crop: %d,%d %d,%d", md.width,
      md.height, rect.left, rect.top, rect.right, rect.bottom);

  if (dec->info.finfo->format == GST_VIDEO_FORMAT_I420) {
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
  } else {
    width = md.width;
    height = md.height;
  }

  GST_INFO_OBJECT (dec, "configuring state: width=%d, height=%d", width,
      height);

  dec->out_state = gst_video_decoder_set_output_state (decoder,
      dec->info.finfo->format, width, height, dec->in_state);

  /* now the caps */
  g_assert (dec->out_state->caps == NULL);
  dec->out_state->caps = gst_video_info_to_caps (&dec->out_state->info);

  if (dec->out_state->info.finfo->format == GST_VIDEO_FORMAT_YV12) {
    GstCapsFeatures *feature =
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_BUFFER,
        NULL);
    gst_caps_set_features (dec->out_state->caps, 0, feature);
  } else {
    memcpy (&dec->crop_rect, &rect, sizeof (rect));
    if (!dec->convert) {
      dec->convert = droid_media_convert_create ();
    }

    if (!dec->convert) {
      GST_ELEMENT_ERROR (dec, LIBRARY, INIT, (NULL),
          ("Failed to create I420 converter"));
      goto error;
    }

    droid_media_convert_set_crop_rect (dec->convert, rect, md.width, md.height);
    GST_INFO_OBJECT (dec, "using I420 conversion for output buffers");
  }

  if (!gst_video_decoder_negotiate (decoder)) {
    GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
        ("Failed to negotiate caps"));
    goto error;
  }

  GST_DEBUG_OBJECT (dec, "output caps %" GST_PTR_FORMAT, dec->out_state->caps);

  return TRUE;

error:
  if (dec->out_state) {
    gst_video_codec_state_unref (dec->out_state);
    dec->out_state = NULL;
  }

  if (dec->convert) {
    droid_media_convert_destroy (dec->convert);
    dec->convert = NULL;
  }

  return FALSE;
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

  if (dec->codec_type) {
    gst_droid_codec_unref (dec->codec_type);
    dec->codec_type = NULL;
  }

  if (dec->convert) {
    droid_media_convert_destroy (dec->convert);
    dec->convert = NULL;
  }

  if (dec->pool) {
    gst_buffer_pool_set_active (dec->pool, FALSE);
  }

  return TRUE;
}

static void
gst_droiddec_stop_loop (GstDroidDec * dec)
{
  /*
   * we cannot use _stop() here because it tries to acquire stream lock
   * which frame_available also needs. This can deadlock because stop
   * won't complete before frame_available is done.
   */
  GST_DEBUG_OBJECT (dec, "stop loop");

  if (dec->pool) {
    gst_buffer_pool_set_flushing (dec->pool, TRUE);
  }

  gst_pad_stop_task (GST_VIDEO_DECODER_SRC_PAD (GST_VIDEO_DECODER (dec)));

  if (dec->pool) {
    gst_buffer_pool_set_flushing (dec->pool, FALSE);
  }
}

static void
gst_droiddec_finalize (GObject * object)
{
  GstDroidDec *dec = GST_DROIDDEC (object);

  GST_DEBUG_OBJECT (dec, "finalize");

  gst_droiddec_stop (GST_VIDEO_DECODER (dec));

  if (dec->pool) {
    gst_object_unref (dec->pool);
    dec->pool = NULL;
  }

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
  dec->dirty = TRUE;

  return TRUE;
}

static gboolean
gst_droiddec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);
  GstCaps *caps, *template_caps;

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

  dec->codec_type =
      gst_droid_codec_new_from_caps (state->caps,
      GST_DROID_CODEC_DECODER_VIDEO);
  if (!dec->codec_type) {
    GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, (NULL),
        ("Unknown codec type for caps %" GST_PTR_FORMAT, state->caps));
    return FALSE;
  }

  /* fill info */
  template_caps =
      gst_pad_get_pad_template_caps (GST_VIDEO_DECODER_SRC_PAD (decoder));
  caps =
      gst_pad_peer_query_caps (GST_VIDEO_DECODER_SRC_PAD (decoder),
      template_caps);
  gst_caps_unref (template_caps);
  template_caps = NULL;

  GST_DEBUG_OBJECT (dec, "peer caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_truncate (caps);
  caps = gst_caps_fixate (caps);

  if (!gst_video_info_from_caps (&dec->info, caps)) {
    GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL), ("Failed to parse caps"));
    gst_caps_unref (caps);
    return FALSE;
  }

  gst_caps_unref (caps);

  dec->in_state = gst_video_codec_state_ref (state);

  if (dec->out_state) {
    gst_video_codec_state_unref (dec->out_state);
    dec->out_state = NULL;
  }

  gst_buffer_replace (&dec->codec_data, state->codec_data);

  /* handle_frame will create the codec */
  dec->dirty = TRUE;

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
  } else {
    goto out;
  }

  /* release the lock to allow _frame_available () to do its job */
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  /* Now we wait for the codec to signal EOS */
  g_cond_wait (&dec->eos_cond, &dec->eos_lock);
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

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
gst_droiddec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);
  GstFlowReturn ret;
  DroidMediaCodecData data;
  DroidMediaBufferCallbacks cb;

  GST_DEBUG_OBJECT (dec, "handle frame");

  if (!GST_CLOCK_TIME_IS_VALID (frame->dts)
      && !GST_CLOCK_TIME_IS_VALID (frame->pts)) {
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
    if (!GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame)) {
      ret = GST_FLOW_OK;
      gst_video_decoder_drop_frame (decoder, frame);
      goto out;
    }

    if (dec->codec) {
      gst_droiddec_finish (decoder);
    }

    if (!gst_droiddec_create_codec (dec)) {
      ret = GST_FLOW_ERROR;
      goto error;
    }

    dec->dirty = FALSE;
  }

  if (!gst_droid_codec_prepare_decoder_frame (dec->codec_type, frame,
          &data.data, &cb)) {
    ret = GST_FLOW_ERROR;
    GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
        ("Failed to prepare data for decoding"));
    goto error;
  }

  /*
   * try to use dts if pts is not valid.
   * on one of the test streams we get the first PTS set to GST_CLOCK_TIME_NONE
   * which breaks timestamping.
   */
  data.ts =
      GST_CLOCK_TIME_IS_VALID (frame->pts) ? GST_TIME_AS_USECONDS (frame->
      pts) : GST_TIME_AS_USECONDS (frame->dts);
  data.sync = GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame) ? true : false;

  /* This can deadlock if droidmedia/stagefright input buffer queue is full thus we
   * cannot write the input buffer. We end up waiting for the write operation
   * which does not happen because stagefright needs us to provide
   * output buffers to be filled (which can not happen because _loop() tries
   * to call get_oldest_frame() which acquires the stream lock the base class
   * is holding before calling us
   */
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  droid_media_codec_queue (dec->codec, &data, &cb);
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

  ret = GST_FLOW_OK;

out:
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

static GstStateChangeReturn
gst_droiddec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDroidDec *dec;

  dec = GST_DROIDDEC (element);

  GST_DEBUG_OBJECT (dec, "change state from %s to %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY) {
    /*
     * _loop() can be waiting in droid_media_codec_loop() thus we must make
     * sure the stagefright decoder is not doing anything otherwise
     * gst_droiddec_stop_loop() will deadlock
     */
    if (dec->codec) {
      GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
      GST_VIDEO_DECODER_STREAM_LOCK (decoder);
      gst_droiddec_finish (decoder);
      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    }
    gst_droiddec_stop_loop (dec);
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
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
  dec->convert = NULL;
  dec->pool = NULL;
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

  caps = gst_droid_codec_get_all_caps (GST_DROID_CODEC_DECODER_VIDEO);
  tpl = gst_pad_template_new (GST_VIDEO_DECODER_SINK_NAME,
      GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (gstelement_class, tpl);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droiddec_src_template_factory));

  gobject_class->finalize = gst_droiddec_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_droiddec_change_state);
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
