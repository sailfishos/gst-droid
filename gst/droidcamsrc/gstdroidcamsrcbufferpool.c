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

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

G_DEFINE_TYPE (GstDroidCamSrcBufferPool, gst_droidcamsrc_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static int
gst_droidcamsrc_buffer_pool_dequeue_buffer (struct preview_stream_ops *w,
    buffer_handle_t ** buffer, int *stride)
{
  // TODO:

  return -1;
}

static int
gst_droidcamsrc_buffer_pool_enqueue_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  // TODO:

  return -1;
}

static int
gst_droidcamsrc_buffer_pool_cancel_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  // TODO:

  return -1;
}

static int
gst_droidcamsrc_buffer_pool_set_buffer_count (struct preview_stream_ops *w,
    int count)
{
  // TODO:

  return -1;
}

static int
gst_droidcamsrc_buffer_pool_set_buffers_geometry (struct preview_stream_ops *pw,
    int w, int h, int format)
{
  // TODO:

  return -1;
}

static int
gst_droidcamsrc_buffer_pool_set_crop (struct preview_stream_ops *w,
    int left, int top, int right, int bottom)
{
  // TODO:

  return -1;
}

static int
gst_droidcamsrc_buffer_pool_set_usage (struct preview_stream_ops *w, int usage)
{
  // TODO:

  return -1;
}

static int
gst_droidcamsrc_buffer_pool_set_swap_interval (struct preview_stream_ops *w,
    int interval)
{
  // TODO:

  return -1;
}

static int
gst_droidcamsrc_buffer_pool_get_min_undequeued_buffer_count (const struct
    preview_stream_ops *w, int *count)
{
  // TODO:

  return -1;
}

static int
gst_droidcamsrc_buffer_pool_lock_buffer (struct preview_stream_ops *w,
    buffer_handle_t * buffer)
{
  // TODO:

  return -1;
}

static int
gst_droidcamsrc_buffer_pool_set_timestamp (struct preview_stream_ops *w,
    int64_t timestamp)
{
  // TODO:

  return -1;
}

GstDroidCamSrcBufferPool *
gst_droid_cam_src_buffer_pool_new ()
{
  return g_object_new (GST_TYPE_DROIDCAMSRC_BUFFER_POOL, NULL);
}

static void
gst_droidcamsrc_buffer_pool_init (GstDroidCamSrcBufferPool * pool)
{
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

}
