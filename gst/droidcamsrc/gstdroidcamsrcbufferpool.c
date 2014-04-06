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

#include "gstdroidcamsrcbufferpool.h"
#include "gst/memory/gstgralloc.h"
#include "gstdroidcamsrc.h"
#include <unistd.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

#define gst_droidcamsrc_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstDroidCamSrcBufferPool, gst_droidcamsrc_buffer_pool,
    GST_TYPE_BUFFER_POOL);

#define ACQUIRE_BUFFER_TRIALS                  4
#define ACAUIRE_BUFFER_TIMEOUT                 10000    /* us */
#define MIN_UNDEQUEUED_BUFFER_COUNT            2
#define GST_DROIDCAMSRC_BUFFER_POOL_USAGE_KEY  "usage"
#define GST_DROIDCAMSRC_BUFFER_POOL_WIDTH_KEY  "width"
#define GST_DROIDCAMSRC_BUFFER_POOL_HEIGHT_KEY "height"
#define GST_DROIDCAMSRC_BUFFER_POOL_FORMAT_KEY "format"
#define GST_DROIDCAMSRC_BUFFER_POOL_COUNT_KEY  "count"
#define GST_DROIDCAMSRC_BUFFER_POOL_LEFT_KEY   "left"
#define GST_DROIDCAMSRC_BUFFER_POOL_RIGHT_KEY  "right"
#define GST_DROIDCAMSRC_BUFFER_POOL_TOP_KEY    "top"
#define GST_DROIDCAMSRC_BUFFER_POOL_BOTTOM_KEY "bottom"

// TODO: keep this in 1 place
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

static gboolean gst_droidcamsrc_buffer_pool_set_config (GstDroidCamSrcBufferPool
    * pool);

static int
gst_droidcamsrc_buffer_pool_dequeue_buffer (struct preview_stream_ops *w,
    buffer_handle_t ** buffer, int *stride)
{
  GstDroidCamSrcBufferPool *pool;
  GstBuffer *buff;
  GstFlowReturn ret;
  int trials;
  GstBufferPoolAcquireParams params;
  GstMemory *mem;
  struct ANativeWindowBuffer *native;

  pool = container_of (w, GstDroidCamSrcBufferPool, window);

  GST_DEBUG_OBJECT (pool, "dequeue buffer");

  if (!gst_buffer_pool_is_active (GST_BUFFER_POOL (pool))) {
    if (!gst_droidcamsrc_buffer_pool_set_config (pool)) {
      return -1;
    }
  }

  GST_LOG_OBJECT (pool, "pool is active");

  mem = NULL;
  trials = ACQUIRE_BUFFER_TRIALS;
  params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;

  while (trials > 0) {
    ret =
        gst_buffer_pool_acquire_buffer (GST_BUFFER_POOL (pool), &buff, &params);
    if (ret == GST_FLOW_OK) {
      /* we have our buffer */
      break;
    } else if (ret == GST_FLOW_ERROR || ret == GST_FLOW_FLUSHING) {
      /* no point in waiting */
      break;
    }

    usleep (ACAUIRE_BUFFER_TIMEOUT);

    --trials;
  }

  if (buff) {
    /* handover */
    mem = gst_buffer_peek_memory (buff, 0);
  } else if (ret == GST_FLOW_FLUSHING) {
    GST_INFO_OBJECT (pool, "pool is flushing");
  } else {
    GST_WARNING_OBJECT (pool, "failed to get a buffer");
  }

  if (!mem) {
    GST_ERROR_OBJECT (pool, "no buffer memory found");
    return -1;
  }

  native = gst_memory_get_native_buffer (mem);
  if (!native) {
    GST_ERROR_OBJECT (pool, "invalid buffer");
    gst_buffer_unref (buff);
    return -1;
  }

  *buffer = &native->handle;
  *stride = native->stride;

  GST_LOG_OBJECT (pool, "dequeue buffer done %p", *buffer);

  g_mutex_lock (&pool->lock);
  g_hash_table_insert (pool->map, *buffer, buff);
  g_mutex_unlock (&pool->lock);

  return 0;
}

static int
gst_droidcamsrc_buffer_pool_enqueue_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  GstDroidCamSrcBufferPool *pool;
  GstBuffer *buff;

  pool = container_of (w, GstDroidCamSrcBufferPool, window);

  GST_LOG_OBJECT (pool, "enqueue buffer %p", buffer);

  g_mutex_lock (&pool->lock);
  buff = g_hash_table_lookup (pool->map, buffer);

  if (!buff) {
    GST_ERROR_OBJECT (pool, "no buffer corresponding to handle %p", buffer);
    g_mutex_unlock (&pool->lock);
    return -1;
  }
  // TODO: pts, dts, duration, offset, offset_end ...
  g_hash_table_remove (pool->map, buffer);
  g_mutex_unlock (&pool->lock);

  g_mutex_lock (&pool->pad->lock);
  if (GST_BUFFER_POOL_IS_FLUSHING (GST_BUFFER_POOL (pool))) {
    gst_buffer_unref (buff);
    GST_DEBUG_OBJECT (pool, "unreffing buffer because buffer pool is flushing");
    goto unlock_and_out;
  }

  if (!pool->pad->running) {
    gst_buffer_unref (buff);
    GST_DEBUG_OBJECT (pool, "unreffing buffer because pad task is not running");
    goto unlock_and_out;
  }

  g_queue_push_tail (pool->pad->queue, buff);
  g_cond_signal (&pool->pad->cond);

unlock_and_out:
  g_mutex_unlock (&pool->pad->lock);

  return 0;
}

static int
gst_droidcamsrc_buffer_pool_cancel_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  GstDroidCamSrcBufferPool *pool;
  pool = container_of (w, GstDroidCamSrcBufferPool, window);

  GST_DEBUG_OBJECT (pool, "cancel buffer");

  // TODO:

  return 0;
}

static int
gst_droidcamsrc_buffer_pool_set_buffer_count (struct preview_stream_ops *w,
    int count)
{
  GstDroidCamSrcBufferPool *pool;
  GstStructure *config;

  pool = container_of (w, GstDroidCamSrcBufferPool, window);

  GST_INFO_OBJECT (pool, "setting buffer count to %d", count);

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL (pool));
  if (!config) {
    GST_ERROR_OBJECT (pool, "failed to get buffer pool config");
    return -1;
  }

  gst_structure_set (config,
      GST_DROIDCAMSRC_BUFFER_POOL_COUNT_KEY, G_TYPE_INT, count, NULL);

  if (!gst_buffer_pool_set_config (GST_BUFFER_POOL (pool), config)) {
    GST_ERROR_OBJECT (pool, "failed to set buffer pool config");
    return -1;
  }

  return 0;
}

static int
gst_droidcamsrc_buffer_pool_set_buffers_geometry (struct preview_stream_ops *pw,
    int w, int h, int format)
{
  GstDroidCamSrcBufferPool *pool;
  GstStructure *config;

  pool = container_of (pw, GstDroidCamSrcBufferPool, window);

  GST_INFO_OBJECT (pool, "setting geometry to to %dx%d/0x%x", w, h, format);

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL (pool));
  if (!config) {
    GST_ERROR_OBJECT (pool, "failed to get buffer pool config");
    return -1;
  }

  gst_structure_set (config,
      GST_DROIDCAMSRC_BUFFER_POOL_WIDTH_KEY, G_TYPE_INT, w,
      GST_DROIDCAMSRC_BUFFER_POOL_HEIGHT_KEY, G_TYPE_INT, h,
      GST_DROIDCAMSRC_BUFFER_POOL_FORMAT_KEY, G_TYPE_INT, format, NULL);

  if (!gst_buffer_pool_set_config (GST_BUFFER_POOL (pool), config)) {
    GST_ERROR_OBJECT (pool, "failed to set buffer pool config");
    return -1;
  }

  return 0;
}

static int
gst_droidcamsrc_buffer_pool_set_crop (struct preview_stream_ops *w,
    int left, int top, int right, int bottom)
{
  GstDroidCamSrcBufferPool *pool;
  GstStructure *config;

  pool = container_of (w, GstDroidCamSrcBufferPool, window);

  GST_INFO_OBJECT (pool, "setting crop to (%d, %d), (%d, %d)", left, top, right,
      bottom);

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL (pool));
  if (!config) {
    GST_ERROR_OBJECT (pool, "failed to get buffer pool config");
    return -1;
  }

  gst_structure_set (config,
      GST_DROIDCAMSRC_BUFFER_POOL_TOP_KEY, G_TYPE_INT, top,
      GST_DROIDCAMSRC_BUFFER_POOL_LEFT_KEY, G_TYPE_INT, left,
      GST_DROIDCAMSRC_BUFFER_POOL_BOTTOM_KEY, G_TYPE_INT, bottom,
      GST_DROIDCAMSRC_BUFFER_POOL_RIGHT_KEY, G_TYPE_INT, right, NULL);

  if (!gst_buffer_pool_set_config (GST_BUFFER_POOL (pool), config)) {
    GST_ERROR_OBJECT (pool, "failed to set buffer pool config");
    return -1;
  }

  return 0;
}

static int
gst_droidcamsrc_buffer_pool_set_usage (struct preview_stream_ops *w, int usage)
{
  GstDroidCamSrcBufferPool *pool;
  GstStructure *config;

  pool = container_of (w, GstDroidCamSrcBufferPool, window);

  GST_INFO_OBJECT (pool, "setting usage to 0x%x", usage);

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL (pool));
  if (!config) {
    GST_ERROR_OBJECT (pool, "failed to get buffer pool config");
    return -1;
  }

  gst_structure_set (config, GST_DROIDCAMSRC_BUFFER_POOL_USAGE_KEY, G_TYPE_INT,
      usage, NULL);

  if (!gst_buffer_pool_set_config (GST_BUFFER_POOL (pool), config)) {
    GST_ERROR_OBJECT (pool, "failed to set buffer pool config");
    return -1;
  }

  return 0;
}

static int
gst_droidcamsrc_buffer_pool_set_swap_interval (struct preview_stream_ops *w,
    int interval)
{
  GstDroidCamSrcBufferPool *pool;

  pool = container_of (w, GstDroidCamSrcBufferPool, window);

  GST_INFO_OBJECT (pool, "ignoring swap interval %d", interval);

  return 0;
}

static int
gst_droidcamsrc_buffer_pool_get_min_undequeued_buffer_count (const struct
    preview_stream_ops *w, int *count)
{
  GstDroidCamSrcBufferPool *pool;

  pool = container_of (w, GstDroidCamSrcBufferPool, window);

  *count = MIN_UNDEQUEUED_BUFFER_COUNT;

  GST_INFO_OBJECT (pool, "setting min undequeued buffer count to %d", *count);

  return 0;
}

static int
gst_droidcamsrc_buffer_pool_lock_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  GstDroidCamSrcBufferPool *pool;

  pool = container_of (w, GstDroidCamSrcBufferPool, window);

  GST_LOG_OBJECT (pool, "lock buffer");
  // TODO:

  return 0;
}

static int
gst_droidcamsrc_buffer_pool_set_timestamp (struct preview_stream_ops *w,
    int64_t timestamp)
{
  // TODO:

  return 0;
}

static void
gst_droidcamsrc_buffer_pool_finalize (GObject * object)
{
  GstDroidCamSrcBufferPool *pool = GST_DROIDCAMSRC_BUFFER_POOL (object);

  g_hash_table_unref (pool->map);
  pool->map = NULL;

  gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  g_mutex_clear (&pool->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_droidcamsrc_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstMemory *mem;
  GstStructure *config;
  int width, height, format, usage;
  GstDroidCamSrcBufferPool *pool = GST_DROIDCAMSRC_BUFFER_POOL (bpool);

  GST_INFO_OBJECT (pool, "alloc buffer");

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL (pool));
  if (!config) {
    GST_ERROR_OBJECT (pool, "failed to get buffer pool config");
    return GST_FLOW_ERROR;
  }

  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_WIDTH_KEY, &width);
  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_HEIGHT_KEY,
      &height);
  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_FORMAT_KEY,
      &format);
  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_USAGE_KEY, &usage);

  gst_structure_free (config);

  usage |= GST_GRALLOC_USAGE_HW_TEXTURE;

  mem = gst_gralloc_allocator_alloc (pool->allocator, width, height,
      format, usage);
  if (!mem) {
    GST_ERROR_OBJECT (pool, "failed to allocate gralloc memory");
    return GST_FLOW_ERROR;
  }

  *buffer = gst_buffer_new ();
  gst_buffer_append_memory (*buffer, mem);

  return GST_FLOW_OK;
}

GstDroidCamSrcBufferPool *
gst_droid_cam_src_buffer_pool_new ()
{
  return g_object_new (GST_TYPE_DROIDCAMSRC_BUFFER_POOL, NULL);
}

static void
gst_droidcamsrc_buffer_pool_init (GstDroidCamSrcBufferPool * pool)
{
  pool->map = g_hash_table_new (g_direct_hash, g_direct_equal);

  pool->allocator = gst_gralloc_allocator_new ();

  g_mutex_init (&pool->lock);

  pool->window.dequeue_buffer = gst_droidcamsrc_buffer_pool_dequeue_buffer;
  pool->window.enqueue_buffer = gst_droidcamsrc_buffer_pool_enqueue_buffer;
  pool->window.cancel_buffer = gst_droidcamsrc_buffer_pool_cancel_buffer;
  pool->window.set_buffer_count = gst_droidcamsrc_buffer_pool_set_buffer_count;
  pool->window.set_buffers_geometry =
      gst_droidcamsrc_buffer_pool_set_buffers_geometry;
  pool->window.set_crop = gst_droidcamsrc_buffer_pool_set_crop;
  pool->window.set_usage = gst_droidcamsrc_buffer_pool_set_usage;
  pool->window.set_swap_interval =
      gst_droidcamsrc_buffer_pool_set_swap_interval;
  pool->window.get_min_undequeued_buffer_count =
      gst_droidcamsrc_buffer_pool_get_min_undequeued_buffer_count;
  pool->window.lock_buffer = gst_droidcamsrc_buffer_pool_lock_buffer;
  pool->window.set_timestamp = gst_droidcamsrc_buffer_pool_set_timestamp;
}

static void
gst_droidcamsrc_buffer_pool_class_init (GstDroidCamSrcBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_droidcamsrc_buffer_pool_finalize;
  gstbufferpool_class->alloc_buffer = gst_droidcamsrc_buffer_pool_alloc_buffer;
}

static gboolean
gst_droidcamsrc_buffer_pool_set_config (GstDroidCamSrcBufferPool * pool)
{
  GstStructure *config;
  int width, height, format, count, usage;
  int left, right, top, bottom;
  GstCaps *caps;
  GstVideoFormat fmt;
  GstVideoInfo info;

  GST_DEBUG_OBJECT (pool, "set config");

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL (pool));
  if (!config) {
    GST_ERROR_OBJECT (pool, "failed to get buffer pool config");
    return FALSE;
  }

  if (!gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_USAGE_KEY,
          &usage)
      || !gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_WIDTH_KEY,
          &width)
      || !gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_HEIGHT_KEY,
          &height)
      || !gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_FORMAT_KEY,
          &format)
      || !gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_COUNT_KEY,
          &count)) {

    GST_ERROR_OBJECT (pool, "incomplete configuration");

    gst_structure_free (config);
    return FALSE;
  }

  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_LEFT_KEY, &left);
  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_RIGHT_KEY, &right);
  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_TOP_KEY, &top);
  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_BOTTOM_KEY,
      &bottom);

  fmt = gst_gralloc_hal_to_gst (format);
  if (fmt == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_WARNING_OBJECT (pool, "unknown hal format 0x%x. using ENCODED instead",
        format);
    fmt = GST_VIDEO_FORMAT_ENCODED;
  }

  gst_video_info_init (&info);
  gst_video_info_set_format (&info, fmt, width, height);

  /* TODO: query from camera parameters */
  GST_VIDEO_INFO_FPS_N (&info) = 30;
  GST_VIDEO_INFO_FPS_D (&info) = 1;

  caps = gst_video_info_to_caps (&info);

  /* set our config */
  gst_buffer_pool_config_set_params (config, caps, 0, count, count);
  gst_buffer_pool_config_set_allocator (config, pool->allocator, NULL);

  if (!gst_buffer_pool_set_config (GST_BUFFER_POOL (pool), config)) {
    GST_ERROR_OBJECT (pool, "failed to set buffer pool config");
    gst_caps_unref (caps);
    return FALSE;
  }
  // TODO: when should we deactivate the pool?
  if (!gst_buffer_pool_set_active (GST_BUFFER_POOL (pool), TRUE)) {
    GST_ERROR_OBJECT (pool, "failed to activate buffer pool");
    gst_caps_unref (caps);
    return FALSE;
  }

  /* wait for the queue to be empty before replacing the caps */
  g_mutex_lock (&pool->pad->lock);
  while (pool->pad->queue->length) {
    g_mutex_unlock (&pool->pad->lock);
    GST_DEBUG_OBJECT (pool, "waiting for pad queue to become empty");
    usleep (ACAUIRE_BUFFER_TIMEOUT);
    g_mutex_lock (&pool->pad->lock);
  }

  gst_caps_replace (&pool->pad->caps, caps);
  gst_caps_unref (caps);
  g_mutex_unlock (&pool->pad->lock);

  return TRUE;
}

void
gst_droid_cam_src_buffer_pool_reset (GstDroidCamSrcBufferPool * pool)
{
  GST_DEBUG_OBJECT (pool, "reset");

  g_mutex_lock (&pool->lock);
  g_hash_table_remove_all (pool->map);
  g_mutex_unlock (&pool->lock);
}
