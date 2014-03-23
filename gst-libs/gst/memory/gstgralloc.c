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

#include <gst/gst.h>
#include "gstgralloc.h"

GST_DEBUG_CATEGORY_STATIC (gralloc_debug);
#define GST_CAT_DEFAULT gralloc_debug

typedef struct
{
  GstAllocator parent;
} GstGrallocAllocator;

typedef struct
{
  GstAllocatorClass parent_class;
} GstGrallocAllocatorClass;

G_DEFINE_TYPE (GstGrallocAllocator, gralloc_mem_allocator, GST_TYPE_ALLOCATOR);

#define GST_TYPE_GRALLOC_ALLOCATOR   (gralloc_mem_allocator_get_type())

GstAllocator * gst_gralloc_allocator_new (void)
{
  GST_DEBUG_CATEGORY_INIT (gralloc_debug, "gralloc", 0, "gralloc memory");

  return g_object_new (GST_TYPE_GRALLOC_ALLOCATOR, NULL);
}

static void
gralloc_mem_allocator_init (GstGrallocAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_ALLOCATOR_GRALLOC;

  // TODO:
  /*
  GstMemoryMapFunction      mem_map;
  GstMemoryUnmapFunction    mem_unmap;

  GstMemoryCopyFunction     mem_copy;
  GstMemoryShareFunction    mem_share;
  GstMemoryIsSpanFunction   mem_is_span;
  */

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static void
gralloc_mem_allocator_class_init (GstGrallocAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = NULL;
  // TODO: free
}

/*

#define GST_ALLOCATOR_GRALLOC "gralloc"



gboolean       gst_is_gralloc_memory (GstMemory * mem);
*/
