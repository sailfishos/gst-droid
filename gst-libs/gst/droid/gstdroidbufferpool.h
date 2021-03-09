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

#ifndef __GST_DROID_BUFFER_POOL_H__
#define __GST_DROID_BUFFER_POOL_H__

#include <gst/gstbufferpool.h>
#include <gst/video/video-info.h>
#include <droidmedia/droidmedia.h>

G_BEGIN_DECLS

typedef struct _GstDroidBufferPool GstDroidBufferPool;
typedef struct _GstDroidBufferPoolClass GstDroidBufferPoolClass;

typedef void *EGLDisplay;

#define GST_TYPE_DROID_BUFFER_POOL      (gst_droid_buffer_pool_get_type())
#define GST_IS_DROID_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DROID_BUFFER_POOL))
#define GST_DROID_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DROID_BUFFER_POOL, GstDroidBufferPool))
#define GST_DROID_BUFFER_POOL_CAST(obj) ((GstDroidBufferPool*)(obj))

struct _GstDroidBufferPool
{
  GstBufferPool parent;
  GstAllocator *allocator;
  GstVideoInfo video_info;
  GPtrArray *bound_buffers;
  GPtrArray *acquired_buffers;
  GMutex binding_lock;
  EGLDisplay display;
  gboolean use_queue_buffers;
};

struct _GstDroidBufferPoolClass
{
  GstBufferPoolClass parent_class;

  void (*signal_buffers_invalidated)   (GstDroidBufferPool *pool);
};

GType             gst_droid_buffer_pool_get_type        (void);
GstBufferPool *   gst_droid_buffer_pool_new             (void);

void       gst_droid_buffer_pool_set_egl_display (GstBufferPool *pool, EGLDisplay display);
gboolean   gst_droid_buffer_pool_bind_media_buffer (GstBufferPool *pool,
                                                    DroidMediaBuffer *buffer);
void       gst_droid_buffer_pool_media_buffers_invalidated (GstBufferPool *pool);
GstBuffer *gst_droid_buffer_pool_acquire_media_buffer (GstBufferPool *pool,
                                                       DroidMediaBuffer *buffer);

G_END_DECLS

#endif /* __GST_DROID_BUFFER_POOL_H__ */
