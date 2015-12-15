/*
 * gst-droid
 *
 * Copyright (C) 2014-2015 Mohammed Sameer <msameer@foolab.org>
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

#ifndef __GST_WRAPPED_MEMORY_H__
#define __GST_WRAPPED_MEMORY_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_ALLOCATOR_WRAPPED_MEMORY                    "WrappedMemory"
#define GST_CAPS_FEATURE_MEMORY_DROID_VIDEO_META_DATA   "memory:DroidVideoMetaData"

GstAllocator * gst_wrapped_memory_allocator_new (void);

gboolean       gst_is_wrapped_memory_memory (GstMemory * mem);
GstMemory    * gst_wrapped_memory_allocator_wrap (GstAllocator * allocator,
						  void *data, gsize size, GFunc cb,
						  gpointer user_data);

G_END_DECLS

#endif /* __GST_WRAPPED_MEMORY_H__ */
