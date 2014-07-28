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

#include "gstdroidcamsrcstreamwindow.h"
#include "gstdroidcamsrcbufferpool.h"
#include "gstdroidcamsrc.h"
#include "gst/memory/gstgralloc.h"
#include "gstdroidmacros.h"
#include <unistd.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droid_camsrc_debug);
#define GST_CAT_DEFAULT gst_droid_camsrc_debug

#define ACQUIRE_BUFFER_TRIALS                  4
#define MIN_UNDEQUEUED_BUFFER_COUNT            2
#define ACQUIRE_BUFFER_TIMEOUT                 10000    /* us */

static GstBuffer *gst_droidcamsrc_stream_window_get_buffer (buffer_handle_t *
    handle);

static void
gst_droidcamsrc_stream_window_reset_buffer_pool_locked (GstDroidCamSrcStreamWindow * win);

static int
gst_droidcamsrc_stream_window_dequeue_buffer (struct preview_stream_ops *w,
    buffer_handle_t ** buffer, int *stride)
{
  GstDroidCamSrcStreamWindow *win;
  GstBuffer *buff;
  GstFlowReturn ret;
  int trials;
  GstBufferPoolAcquireParams params;
  GstMemory *mem;
  struct ANativeWindowBuffer *native;
  int res;

  GST_DEBUG ("dequeue buffer %p", buffer);

  win = container_of (w, GstDroidCamSrcStreamWindow, window);

  g_mutex_lock (&win->lock);

retry:
  GST_DEBUG ("needs reconfigure? %d", win->needs_reconfigure);

  if (!win->pool || (win->pool && win->needs_reconfigure)) {
    /* create and re/configure the pool */
    gst_droidcamsrc_stream_window_reset_buffer_pool_locked (win);
  }

  if (!win->pool) {
    GST_ERROR ("failed to create buffer pool");
    res = -1;
    goto unlock_and_exit;
  }

  mem = NULL;
  trials = ACQUIRE_BUFFER_TRIALS;
  params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;

  while (trials > 0) {
    ret =
        gst_buffer_pool_acquire_buffer (GST_BUFFER_POOL (win->pool), &buff,
        &params);
    if (ret == GST_FLOW_OK) {
      /* we have our buffer */
      break;
    } else if (ret == GST_FLOW_ERROR || ret == GST_FLOW_FLUSHING) {
      /* no point in waiting */
      break;
    }

    /* we need to unlock here to allow buffers to be returned back */
    g_mutex_unlock (&win->lock);
    usleep (ACQUIRE_BUFFER_TIMEOUT);
    g_mutex_lock (&win->lock);
    if (win->needs_reconfigure) {
      /* out of here */
      goto retry;
    }

    --trials;
  }

  if (buff) {
    /* handover */
    mem = gst_buffer_peek_memory (buff, 0);
  } else if (ret == GST_FLOW_FLUSHING) {
    GST_INFO ("pool is flushing");
  } else {
    GST_WARNING ("failed to get a buffer");
  }

  if (!mem) {
    GST_ERROR ("no buffer memory found");

    res = -1;
    goto unlock_and_exit;
  }

  native = gst_memory_get_native_buffer (mem);
  if (!native) {
    GST_ERROR ("invalid buffer");
    gst_buffer_unref (buff);

    res = -1;
    goto unlock_and_exit;
  }

  *buffer = &native->handle;
  *stride = native->stride;

  GST_LOG ("dequeue buffer done %p", *buffer);

  res = 0;

unlock_and_exit:
  g_mutex_unlock (&win->lock);
  return res;
}

static int
gst_droidcamsrc_stream_window_enqueue_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  GstDroidCamSrcStreamWindow *win;
  GstDroidCamSrc *src;
  GstBuffer *buff;
  int ret;
  GstVideoCropMeta *meta;

  GST_DEBUG ("enqueue buffer %p", buffer);

  win = container_of (w, GstDroidCamSrcStreamWindow, window);

  g_mutex_lock (&win->lock);

  src = GST_DROIDCAMSRC (GST_PAD_PARENT (win->pad->pad));

  buff = gst_droidcamsrc_stream_window_get_buffer (buffer);

  if (!buff) {
    GST_ERROR ("no buffer corresponding to handle %p", buffer);
    ret = -1;
    goto unlock_and_out;
  }

  /* if the buffer pool is not our current pool then just release it */
  if (buff->pool != GST_BUFFER_POOL (win->pool)) {
    GST_DEBUG ("releasing old buffer %p", buffer);
    gst_buffer_unref (buff);
    ret = 0;
    goto unlock_and_out;
  }

  /* now update crop meta */
  meta = gst_buffer_get_video_crop_meta (buff);
  meta->x = win->left;
  meta->y = win->top;
  meta->width = win->right - win->left;
  meta->height = win->bottom - win->top;

  GST_LOG
      ("window width = %d, height = %d, crop info: left = %d, top = %d, right = %d, bottom = %d",
      win->width, win->height, win->left, win->top, win->right, win->bottom);

  g_mutex_unlock (&win->lock);

  /* it should be safe to access that variable without locking.
   * pad gets activated during READY_TO_PAUSED and deactivated during
   * PAUSED_TO_READY while we start the preview during PAUSED_TO_PLAYING
   * and stop it during PLAYING_TO_PAUSED.
   */
  if (!win->pad->running) {
    gst_buffer_unref (buff);
    GST_DEBUG ("unreffing buffer because pad task is not running");
    ret = 0;
    goto unlock_pad_and_out;
  }
  // TODO: duration, offset, offset_end ...
  gst_droidcamsrc_timestamp (src, buff);

  g_mutex_lock (&win->pad->queue_lock);

  g_queue_push_tail (win->pad->queue, buff);

  g_cond_signal (&win->pad->cond);

  ret = 0;
  goto unlock_pad_and_out;

unlock_and_out:
  g_mutex_unlock (&win->lock);

  return ret;

unlock_pad_and_out:
  g_mutex_unlock (&win->pad->queue_lock);

  return ret;
}

static int
gst_droidcamsrc_stream_window_cancel_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  GstDroidCamSrcStreamWindow *win;
  GstBuffer *buff;
  int ret;

  GST_DEBUG ("cancel buffer");

  win = container_of (w, GstDroidCamSrcStreamWindow, window);

  g_mutex_lock (&win->lock);

  buff = gst_droidcamsrc_stream_window_get_buffer (buffer);

  if (!buff) {
    GST_ERROR ("no buffer corresponding to handle %p", buffer);
    ret = -1;
    goto unlock_and_out;
  }

  /* back to the pool */
  gst_buffer_unref (buff);
  ret = 0;

unlock_and_out:
  g_mutex_unlock (&win->lock);
  return ret;
}

static int
gst_droidcamsrc_stream_window_set_buffer_count (struct preview_stream_ops *w,
    int count)
{
  GstDroidCamSrcStreamWindow *win;
  win = container_of (w, GstDroidCamSrcStreamWindow, window);

  GST_INFO ("setting buffer count to %d", count);

  g_mutex_lock (&win->lock);
  win->count = count;
  win->needs_reconfigure = TRUE;
  g_mutex_unlock (&win->lock);

  return 0;
}

static int
gst_droidcamsrc_stream_window_set_buffers_geometry (struct preview_stream_ops
    *pw, int w, int h, int format)
{
  GstDroidCamSrcStreamWindow *win;
  win = container_of (pw, GstDroidCamSrcStreamWindow, window);

  GST_INFO ("setting geometry to to %dx%d/0x%x", w, h, format);

  g_mutex_lock (&win->lock);
  win->width = w;
  win->height = h;
  win->format = format;
  win->needs_reconfigure = TRUE;
  g_mutex_unlock (&win->lock);

  return 0;
}

static int
gst_droidcamsrc_stream_window_set_crop (struct preview_stream_ops *w,
    int left, int top, int right, int bottom)
{
  GstDroidCamSrcStreamWindow *win;
  win = container_of (w, GstDroidCamSrcStreamWindow, window);

  GST_INFO ("setting crop to (%d, %d), (%d, %d)", left, top, right, bottom);

  g_mutex_lock (&win->lock);
  win->left = left;
  win->top = top;
  win->right = right;
  win->bottom = bottom;
  g_mutex_unlock (&win->lock);

  return 0;
}

static int
gst_droidcamsrc_stream_window_set_usage (struct preview_stream_ops *w,
    int usage)
{
  GstDroidCamSrcStreamWindow *win;
  win = container_of (w, GstDroidCamSrcStreamWindow, window);

  GST_INFO ("setting usage to 0x%x", usage);

  g_mutex_lock (&win->lock);
  win->usage = usage;
  win->needs_reconfigure = TRUE;
  g_mutex_unlock (&win->lock);

  return 0;
}

static int
gst_droidcamsrc_stream_window_set_swap_interval (struct preview_stream_ops *w,
    int interval)
{
  GST_FIXME ("ignoring swap interval %d", interval);

  return 0;
}

static int
gst_droidcamsrc_stream_window_get_min_undequeued_buffer_count (const struct
    preview_stream_ops *w, int *count)
{
  *count = MIN_UNDEQUEUED_BUFFER_COUNT;

  GST_INFO ("setting min undequeued buffer count to %d", *count);

  return 0;
}

static int
gst_droidcamsrc_stream_window_lock_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  GST_FIXME ("lock buffer");
  // TODO:

  return 0;
}

static int
gst_droidcamsrc_stream_window_set_timestamp (struct preview_stream_ops *w,
    int64_t timestamp)
{
  GST_FIXME ("set timestamp");
  // TODO:

  return 0;
}

GstDroidCamSrcStreamWindow *
gst_droid_cam_src_stream_window_new (GstDroidCamSrcPad * pad,
    GstDroidCamSrcCamInfo * info)
{
  GstDroidCamSrcStreamWindow *win;

  GST_DEBUG ("stream window new");

  win = g_slice_new0 (GstDroidCamSrcStreamWindow);
  win->pad = pad;
  win->allocator = gst_gralloc_allocator_new ();
  win->pool = NULL;
  win->info = info;
  g_mutex_init (&win->lock);

  win->window.dequeue_buffer = gst_droidcamsrc_stream_window_dequeue_buffer;
  win->window.enqueue_buffer = gst_droidcamsrc_stream_window_enqueue_buffer;
  win->window.cancel_buffer = gst_droidcamsrc_stream_window_cancel_buffer;
  win->window.set_buffer_count = gst_droidcamsrc_stream_window_set_buffer_count;
  win->window.set_buffers_geometry =
      gst_droidcamsrc_stream_window_set_buffers_geometry;
  win->window.set_crop = gst_droidcamsrc_stream_window_set_crop;
  win->window.set_usage = gst_droidcamsrc_stream_window_set_usage;
  win->window.set_swap_interval =
      gst_droidcamsrc_stream_window_set_swap_interval;
  win->window.get_min_undequeued_buffer_count =
      gst_droidcamsrc_stream_window_get_min_undequeued_buffer_count;
  win->window.lock_buffer = gst_droidcamsrc_stream_window_lock_buffer;
  win->window.set_timestamp = gst_droidcamsrc_stream_window_set_timestamp;

  return win;
}

void
gst_droid_cam_src_stream_window_destroy (GstDroidCamSrcStreamWindow * win)
{
  GST_DEBUG ("stream window destroy");

  gst_object_unref (win->allocator);
  win->allocator = NULL;

  g_mutex_clear (&win->lock);

  if (win->pool) {
    if (!gst_buffer_pool_set_active (GST_BUFFER_POOL (win->pool), FALSE)) {
      GST_ERROR ("failed to deactivate buffer pool");
    }

    gst_object_unref (win->pool);
  }

  g_slice_free (GstDroidCamSrcStreamWindow, win);
}

static void
gst_droidcamsrc_stream_window_reset_buffer_pool_locked
    (GstDroidCamSrcStreamWindow * win)
{
  GstStructure *config;
  GstCaps *caps;
  GstCapsFeatures *feature;
  GstVideoFormat fmt;
  const gchar *format;

  GST_DEBUG ("stream window configure buffer pool");

  if (win->pool) {
    /* we will ignore the error here */
    if (!gst_buffer_pool_set_active (GST_BUFFER_POOL (win->pool), FALSE)) {
      GST_WARNING ("Failed to deactivate buffer pool");
    }

    gst_object_unref (win->pool);
  }

  win->pool = gst_droid_cam_src_buffer_pool_new (win->info);

  if (!win->count || !win->width || !win->height || !win->usage || !win->format) {
    GST_ERROR ("incomplete configuration");
    goto clean_and_out;
  }

  fmt = gst_gralloc_hal_to_gst (win->format);
  if (fmt == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_WARNING ("unknown hal format 0x%x. using ENCODED instead", win->format);
    fmt = GST_VIDEO_FORMAT_ENCODED;
  }

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL (win->pool));
  if (!config) {
    GST_ERROR ("failed to get buffer pool config");
    goto clean_and_out;
  }
  format = gst_video_format_to_string (fmt);
  /* TODO: 30 is hardcoded */
  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, format,
      "width", G_TYPE_INT, win->width,
      "height", G_TYPE_INT, win->height,
      "framerate", GST_TYPE_FRACTION, 30, 1, NULL);
  feature = gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DROID_HANDLE, NULL);
  gst_caps_set_features (caps, 0, feature);

  gst_buffer_pool_config_set_params (config, caps, -1, win->count, win->count);
  gst_buffer_pool_config_set_allocator (config, win->allocator, NULL);

  gst_structure_set (config,
      GST_DROIDCAMSRC_BUFFER_POOL_USAGE_KEY, G_TYPE_INT, win->usage,
      GST_DROIDCAMSRC_BUFFER_POOL_WIDTH_KEY, G_TYPE_INT, win->width,
      GST_DROIDCAMSRC_BUFFER_POOL_HEIGHT_KEY, G_TYPE_INT, win->height,
      GST_DROIDCAMSRC_BUFFER_POOL_FORMAT_KEY, G_TYPE_INT, win->format, NULL);

  gst_caps_unref (caps);

  if (!gst_buffer_pool_set_config (GST_BUFFER_POOL (win->pool), config)) {
    GST_ERROR ("failed to set buffer pool config");
    goto clean_and_out;
  }

  if (!gst_buffer_pool_set_active (GST_BUFFER_POOL (win->pool), TRUE)) {
    GST_ERROR ("failed to activate buffer pool");
    goto clean_and_out;
  }

  win->needs_reconfigure = FALSE;

  return;

clean_and_out:
  if (win->pool) {
    gst_object_unref (win->pool);
    win->pool = NULL;
  }
}

void
gst_droid_cam_src_stream_window_clear (GstDroidCamSrcStreamWindow * win)
{
  GST_DEBUG ("stream window clear");

  g_mutex_lock (&win->lock);

  if (win->pool) {
    if (!gst_buffer_pool_set_active (GST_BUFFER_POOL (win->pool), FALSE)) {
      GST_ERROR ("failed to deactivate buffer pool");
    }

    gst_object_unref (win->pool);

    win->pool = NULL;
  }

  g_mutex_unlock (&win->lock);
}

static GstBuffer *
gst_droidcamsrc_stream_window_get_buffer (buffer_handle_t * handle)
{
  GstMemory *mem;
  GstBuffer *buff;
  struct ANativeWindowBuffer *buffer =
      container_of (handle, struct ANativeWindowBuffer, handle);

  mem = gst_memory_from_native_buffer (buffer);
  buff = gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
      g_quark_from_string (GST_DROIDCAMSRC_BUFFER_POOL_QDATA));

  return buff;
}
