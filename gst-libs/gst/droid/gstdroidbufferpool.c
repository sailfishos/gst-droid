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

#include <gst/gst.h>
#include "gstdroidbufferpool.h"
#include "gstdroidmediabuffer.h"

#define gst_droid_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstDroidBufferPool, gst_droid_buffer_pool, GST_TYPE_BUFFER_POOL);

static void
gst_droid_buffer_pool_reset_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  GstDroidBufferPool *dpool = GST_DROID_BUFFER_POOL (pool);

  // if the allocator is set then we should keep the memory
  if (!dpool->allocator) {
    gst_buffer_remove_all_memory (buffer);
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);
  }

  g_mutex_lock (&dpool->lock);
  ++dpool->num_buffers;
  GST_DEBUG_OBJECT (dpool, "num buffers: %d", dpool->num_buffers);
  g_cond_signal (&dpool->cond);
  g_mutex_unlock (&dpool->lock);

  return GST_BUFFER_POOL_CLASS (parent_class)->reset_buffer (pool, buffer);
}

static gboolean
gst_droid_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config)
{
  GstCaps *caps;
  guint size;
  GstAllocationParams params = { GST_MEMORY_FLAG_NO_SHARE, 0, 0, 0 };
  GstDroidBufferPool *pool = GST_DROID_BUFFER_POOL (bpool);

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, NULL, NULL)) {
    GST_WARNING_OBJECT (pool, "Invalid pool configuration");
    return FALSE;
  }
  // If we have caps then we can create droid buffers
  if (caps != NULL) {
    GST_OBJECT_LOCK (pool);

    if (!gst_video_info_from_caps (&pool->video_info, caps)) {
      GST_OBJECT_UNLOCK (pool);
      GST_WARNING_OBJECT (pool, "Invalid video caps %" GST_PTR_FORMAT, caps);
      return FALSE;
    }

    pool->video_info.size = size;

    GST_DEBUG_OBJECT (pool, "Configured pool. caps: %" GST_PTR_FORMAT, caps);
    pool->allocator = gst_droid_media_buffer_allocator_new ();
    gst_object_ref (pool->allocator);
    gst_buffer_pool_config_set_allocator (config,
        (GstAllocator *) pool->allocator, &params);
    GST_OBJECT_UNLOCK (pool);
  }

  return GST_BUFFER_POOL_CLASS (gst_droid_buffer_pool_parent_class)
      ->set_config (bpool, config);
}

static const gchar **
gst_droid_buffer_pool_get_options (GstBufferPool * bpool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL };

  return options;
}

static GstFlowReturn
gst_droid_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buf,
    GstBufferPoolAcquireParams * params G_GNUC_UNUSED)
{
  GstDroidBufferPool *dpool = GST_DROID_BUFFER_POOL (pool);
  GstBuffer *buffer;
  GstMemory *memory;

  buffer = gst_buffer_new ();
  if (!buffer) {
    return GST_FLOW_ERROR;
  }
  // if the allocator is set then we should create the memory too
  if (dpool->allocator) {
    memory = gst_droid_media_buffer_allocator_alloc_new (dpool->allocator,
        &dpool->video_info, buffer);
    if (!memory) {
      return GST_FLOW_ERROR;
    }
  }

  g_mutex_lock (&dpool->lock);
  ++dpool->num_buffers;
  *buf = buffer;
  GST_DEBUG_OBJECT (dpool, "num buffers: %d", dpool->num_buffers);
  g_cond_signal (&dpool->cond);
  g_mutex_unlock (&dpool->lock);

  return GST_FLOW_OK;
}

gboolean
gst_droid_buffer_pool_wait_for_buffer (GstBufferPool * pool)
{
  GstDroidBufferPool *dpool = GST_DROID_BUFFER_POOL (pool);

  if (GST_BUFFER_POOL_IS_FLUSHING (pool)) {
    return FALSE;
  }

  g_mutex_lock (&dpool->lock);

  if (dpool->num_buffers > 0) {
    g_mutex_unlock (&dpool->lock);
    return TRUE;
  }

  g_cond_wait (&dpool->cond, &dpool->lock);
  g_mutex_unlock (&dpool->lock);

  if (GST_BUFFER_POOL_IS_FLUSHING (pool)) {
    return FALSE;
  }

  return TRUE;
}

static void
gst_droid_buffer_pool_flush_start (GstBufferPool * pool)
{
  GstDroidBufferPool *dpool = GST_DROID_BUFFER_POOL (pool);

  g_mutex_lock (&dpool->lock);
  g_cond_signal (&dpool->cond);
  g_mutex_unlock (&dpool->lock);
}

static GstFlowReturn
gst_droid_buffer_pool_acquire_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstDroidBufferPool *dpool = GST_DROID_BUFFER_POOL (pool);
  GstFlowReturn ret;

  ret =
      GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (pool, buffer,
      params);

  if (G_LIKELY (ret == GST_FLOW_OK)) {
    g_mutex_lock (&dpool->lock);
    --dpool->num_buffers;
    GST_DEBUG_OBJECT (dpool, "num buffers: %d", dpool->num_buffers);
    g_mutex_unlock (&dpool->lock);
  }

  return ret;
}

static gboolean
gst_droid_buffer_pool_start (GstBufferPool * pool)
{
  GstDroidBufferPool *dpool = GST_DROID_BUFFER_POOL (pool);

  g_mutex_lock (&dpool->lock);
  dpool->num_buffers = 0;
  GST_DEBUG_OBJECT (dpool, "num buffers: %d", dpool->num_buffers);
  g_mutex_unlock (&dpool->lock);

  return GST_BUFFER_POOL_CLASS (parent_class)->start (pool);
}

static void
gst_droid_buffer_pool_finalize (GObject * object)
{
  GstDroidBufferPool *pool = GST_DROID_BUFFER_POOL (object);
  if (pool->allocator) {
    gst_object_unref (pool->allocator);
    pool->allocator = 0;
  }
  g_mutex_clear (&pool->lock);
  g_cond_clear (&pool->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_droid_buffer_pool_class_init (GstDroidBufferPoolClass * klass G_GNUC_UNUSED)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_droid_buffer_pool_finalize;
  gstbufferpool_class->start = gst_droid_buffer_pool_start;
  gstbufferpool_class->acquire_buffer = gst_droid_buffer_pool_acquire_buffer;
  gstbufferpool_class->reset_buffer = gst_droid_buffer_pool_reset_buffer;
  gstbufferpool_class->alloc_buffer = gst_droid_buffer_pool_alloc;
  gstbufferpool_class->flush_start = gst_droid_buffer_pool_flush_start;
  gstbufferpool_class->get_options = gst_droid_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_droid_buffer_pool_set_config;
}

static void
gst_droid_buffer_pool_init (GstDroidBufferPool * object)
{
  GstDroidBufferPool *pool = GST_DROID_BUFFER_POOL (object);
  pool->allocator = 0;
  g_mutex_init (&pool->lock);
  g_cond_init (&pool->cond);
}

GstBufferPool *
gst_droid_buffer_pool_new ()
{
  GstDroidBufferPool *pool;

  pool = g_object_new (GST_TYPE_DROID_BUFFER_POOL, NULL);

  return GST_BUFFER_POOL_CAST (pool);
}
