/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer
 * Copyright (C) 2015-2020 Jolla Ltd.
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

/* Element signals and args */
enum
{
  BUFFERS_INVALIDATED,
  LAST_SIGNAL
};
#define gst_droid_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstDroidBufferPool, gst_droid_buffer_pool, GST_TYPE_BUFFER_POOL);

static guint gst_droid_buffer_pool_signals[LAST_SIGNAL] = { 0 };

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
    GstCapsFeatures *features = gst_caps_get_features (caps, 0);

    GST_OBJECT_LOCK (pool);

    if (!gst_video_info_from_caps (&pool->video_info, caps)) {
      GST_OBJECT_UNLOCK (pool);
      GST_WARNING_OBJECT (pool, "Invalid video caps %" GST_PTR_FORMAT, caps);
      return FALSE;
    }

    pool->video_info.size = size;
    pool->use_queue_buffers = gst_caps_features_contains
        (features, GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_QUEUE_BUFFER);

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

  if (!dpool->allocator) {
    return GST_FLOW_ERROR;
  }

  buffer = gst_buffer_new ();
  if (!buffer) {
    return GST_FLOW_ERROR;
  }

  if (!dpool->use_queue_buffers) {
    GstVideoInfo *video_info;
    GstMemory *memory =
        gst_droid_media_buffer_allocator_alloc_new (dpool->allocator,
        &dpool->video_info);
    if (!memory) {
      gst_buffer_unref (buffer);
      return GST_FLOW_ERROR;
    }

    gst_buffer_insert_memory (buffer, 0, memory);

    video_info = gst_droid_media_buffer_get_video_info (memory);

    gst_buffer_add_video_meta_full (buffer,
        GST_VIDEO_FRAME_FLAG_NONE, video_info->finfo->format,
        video_info->width, video_info->height,
        video_info->finfo->n_planes, video_info->offset, video_info->stride);
  }

  *buf = buffer;

  return GST_FLOW_OK;
}

static void
gst_droid_buffer_release_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  GstDroidBufferPool *dpool = GST_DROID_BUFFER_POOL (pool);
  DroidMediaBuffer *droid_buffer = NULL;

  if (dpool->use_queue_buffers) {
    guint index;

    g_mutex_lock (&dpool->binding_lock);

    if (g_ptr_array_find (dpool->acquired_buffers, buffer, &index)) {
      g_ptr_array_remove_index_fast (dpool->acquired_buffers, index);

      droid_buffer =
          gst_droid_media_buffer_memory_get_buffer_from_gst_buffer (buffer);

      if (droid_media_buffer_get_user_data (droid_buffer) == buffer) {
        g_ptr_array_add (dpool->bound_buffers, buffer);
      } else {
        droid_buffer = NULL;
      }
    }

    g_mutex_unlock (&dpool->binding_lock);
  }

  if (droid_buffer) {
    buffer->pool = gst_object_ref (pool);

    droid_media_buffer_release (droid_buffer, dpool->display, NULL);
  } else {
    if (dpool->use_queue_buffers) {
      gst_buffer_remove_all_memory (buffer);
      GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);
    }

    GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (pool, buffer);
  }
}

void
gst_droid_buffer_pool_set_egl_display (GstBufferPool * pool, EGLDisplay display)
{
  GstDroidBufferPool *dpool = GST_DROID_BUFFER_POOL (pool);

  if (dpool) {
    dpool->display = display;
  }
}

gboolean
gst_droid_buffer_pool_bind_media_buffer (GstBufferPool * pool,
    DroidMediaBuffer * buffer)
{
  GstDroidBufferPool *dpool;
  GstBuffer *gst_buffer;
  GstMemory *mem;

  if (!GST_IS_DROID_BUFFER_POOL (pool)) {
    return FALSE;
  }

  if (gst_buffer_pool_acquire_buffer (pool, &gst_buffer, NULL) != GST_FLOW_OK) {
    return FALSE;
  }

  dpool = GST_DROID_BUFFER_POOL (pool);

  mem =
      gst_droid_media_buffer_allocator_alloc_from_buffer (dpool->allocator,
      buffer);

  if (!mem) {
    gst_buffer_unref (gst_buffer);

    return FALSE;
  }

  gst_buffer_insert_memory (gst_buffer, 0, mem);

  g_mutex_lock (&dpool->binding_lock);

  droid_media_buffer_set_user_data (buffer, gst_buffer);

  g_ptr_array_add (dpool->bound_buffers, gst_buffer);

  g_mutex_unlock (&dpool->binding_lock);

  return TRUE;
}

static void
gst_droid_buffer_pool_clear_buffers_user_data (GPtrArray * buffers)
{
  guint i;

  for (i = 0; i < buffers->len; ++i) {
    DroidMediaBuffer *buffer =
        gst_droid_media_buffer_memory_get_buffer_from_gst_buffer ((GstBuffer *)
        g_ptr_array_index (buffers, i));

    if (buffer) {
      droid_media_buffer_set_user_data (buffer, NULL);
    }
  }
}

GstBuffer *
gst_droid_buffer_pool_acquire_media_buffer (GstBufferPool * pool,
    DroidMediaBuffer * buffer)
{
  guint index;
  GstBuffer *gst_buffer = NULL;
  GstDroidBufferPool *dpool = GST_DROID_BUFFER_POOL (pool);

  if (!dpool) {
    return NULL;
  }

  g_mutex_lock (&dpool->binding_lock);

  gst_buffer = (GstBuffer *) droid_media_buffer_get_user_data (buffer);

  if (gst_buffer->pool != pool) {
    droid_media_buffer_set_user_data (buffer, NULL);

    g_mutex_unlock (&dpool->binding_lock);

    if (!gst_droid_buffer_pool_bind_media_buffer (pool, buffer)) {
      return NULL;
    }

    if (gst_buffer) {
      gst_buffer_unref (gst_buffer);
    }

    g_mutex_lock (&dpool->binding_lock);

    gst_buffer = (GstBuffer *) droid_media_buffer_get_user_data (buffer);
  }

  if (gst_buffer && g_ptr_array_find (dpool->bound_buffers, gst_buffer, &index)) {
    g_ptr_array_remove_index_fast (dpool->bound_buffers, index);

    g_ptr_array_add (dpool->acquired_buffers, gst_buffer);
  }

  g_mutex_unlock (&dpool->binding_lock);

  return gst_buffer;
}

void
gst_droid_buffer_pool_media_buffers_invalidated (GstBufferPool * pool)
{
  GPtrArray *buffers_to_release = NULL;
  guint i;
  GstDroidBufferPool *dpool;

  if (!pool || !GST_IS_DROID_BUFFER_POOL (pool)) {
    GST_WARNING_OBJECT (pool, "Pool is not a GstDroidBufferPool");
    return;
  }

  dpool = GST_DROID_BUFFER_POOL (pool);

  g_mutex_lock (&dpool->binding_lock);

  gst_droid_buffer_pool_clear_buffers_user_data (dpool->bound_buffers);
  gst_droid_buffer_pool_clear_buffers_user_data (dpool->acquired_buffers);

  if (dpool->bound_buffers->len != 0) {
    buffers_to_release = dpool->bound_buffers;

    dpool->bound_buffers = g_ptr_array_new ();
  }

  g_ptr_array_set_size (dpool->acquired_buffers, 0);

  g_mutex_unlock (&dpool->binding_lock);

  if (buffers_to_release) {
    for (i = 0; i < buffers_to_release->len; ++i) {
      gst_buffer_unref ((GstBuffer *) g_ptr_array_index (buffers_to_release,
              i));
    }
    g_ptr_array_free (buffers_to_release, TRUE);
  }

  g_signal_emit (pool, gst_droid_buffer_pool_signals[BUFFERS_INVALIDATED], 0);
}

static void
gst_droid_buffer_pool_finalize (GObject * object)
{
  GstDroidBufferPool *pool = GST_DROID_BUFFER_POOL (object);
  if (pool->allocator) {
    gst_object_unref (pool->allocator);
    pool->allocator = 0;
  }

  g_ptr_array_free (pool->bound_buffers, TRUE);
  g_ptr_array_free (pool->acquired_buffers, TRUE);

  g_mutex_clear (&pool->binding_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_droid_buffer_pool_class_init (GstDroidBufferPoolClass * klass G_GNUC_UNUSED)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_droid_buffer_pool_finalize;
  gstbufferpool_class->alloc_buffer = gst_droid_buffer_pool_alloc;
  gstbufferpool_class->release_buffer = gst_droid_buffer_release_buffer;
  gstbufferpool_class->get_options = gst_droid_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_droid_buffer_pool_set_config;

  gst_droid_buffer_pool_signals[BUFFERS_INVALIDATED] =
      g_signal_new ("buffers-invalidated",
      G_TYPE_FROM_CLASS (gstbufferpool_class), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstDroidBufferPoolClass, signal_buffers_invalidated),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 0);
}

static void
gst_droid_buffer_pool_init (GstDroidBufferPool * object)
{
  GstDroidBufferPool *pool = GST_DROID_BUFFER_POOL (object);
  pool->allocator = 0;

  g_mutex_init (&pool->binding_lock);

  pool->bound_buffers = g_ptr_array_new ();
  pool->acquired_buffers = g_ptr_array_new ();
  pool->use_queue_buffers = FALSE;
  pool->display = NULL;
}

GstBufferPool *
gst_droid_buffer_pool_new ()
{
  GstDroidBufferPool *pool;

  pool = g_object_new (GST_TYPE_DROID_BUFFER_POOL, NULL);

  return GST_BUFFER_POOL_CAST (pool);
}
