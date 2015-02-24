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

#ifndef __GST_DROID_DEC_BUFFER_POOL_H__
#define __GST_DROID_DEC_BUFFER_POOL_H__

#include <gst/gstbufferpool.h>

G_BEGIN_DECLS

typedef struct _GstDroidDecBufferPool GstDroidDecBufferPool;
typedef struct _GstDroidDecBufferPoolClass GstDroidDecBufferPoolClass;

#define GST_TYPE_DROIDDEC_BUFFER_POOL      (gst_droiddec_buffer_pool_get_type())
#define GST_IS_DROIDDEC_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DROIDDEC_BUFFER_POOL))
#define GST_DROIDDEC_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DROIDDEC_BUFFER_POOL, GstDroidDecBufferPool))
#define GST_DROIDDEC_BUFFER_POOL_CAST(obj) ((GstDroidDecBufferPool*)(obj))

struct _GstDroidDecBufferPool
{
  GstBufferPool parent;
  GMutex lock;
  GCond cond;
  gint num_buffers;
};

struct _GstDroidDecBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType             gst_droiddec_buffer_pool_get_type        (void);
GstBufferPool *   gst_droiddec_buffer_pool_new             (void);
gboolean          gst_droiddec_buffer_pool_wait_for_buffer (GstBufferPool * pool);

G_END_DECLS

#endif /* __GST_DROID_DEC_BUFFER_POOL_H__ */
