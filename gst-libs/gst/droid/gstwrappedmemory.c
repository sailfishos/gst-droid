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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include "gstwrappedmemory.h"

GST_DEBUG_CATEGORY_STATIC (wrapped_memory_debug);
#define GST_CAT_DEFAULT wrapped_memory_debug

typedef struct
{
  GstAllocator parent;

} GstWrappedMemoryAllocator;

typedef struct
{
  GstAllocatorClass parent_class;

} GstWrappedMemoryAllocatorClass;

typedef struct
{
  GstMemory mem;

  void *data;
  GFunc cb;
  gpointer user_data;

} GstWrappedMemory;

#define wrapped_memory_allocator_parent_class parent_class
G_DEFINE_TYPE (GstWrappedMemoryAllocator, wrapped_memory_allocator,
    GST_TYPE_ALLOCATOR);

#define GST_TYPE_WRAPPED_MEMORY_ALLOCATOR    (wrapped_memory_allocator_get_type())
#define GST_IS_WRAPPED_MEMORY_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_WRAPPED_MEMORY_ALLOCATOR))
#define GST_WRAPPED_MEMORY_ALLOCATOR(obj)    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WRAPPED_MEMORY_ALLOCATOR,GstWrappedMemoryAllocator))

static void gst_wrapped_memory_allocator_free (GstAllocator * allocator,
    GstMemory * mem);
static gpointer gst_wrapped_memory_map (GstMemory * mem, gsize maxsize,
    GstMapFlags flags);
static void gst_wrapped_memory_unmap (GstMemory * mem);
static GstMemory *gst_wrapped_memory_allocator_memory_new (GstAllocator *
    allocator, gsize size);
static void gst_wrapped_memory_allocator_memory_set_data (GstMemory * mem,
    gpointer data, GFunc cb, gpointer user_data);

GstAllocator *
gst_wrapped_memory_allocator_new (void)
{
  GST_DEBUG_CATEGORY_INIT (wrapped_memory_debug, "wrappedmemory", 0,
      "wrapped memory allocator");

  return g_object_new (GST_TYPE_WRAPPED_MEMORY_ALLOCATOR, NULL);
}

static void
wrapped_memory_allocator_init (GstWrappedMemoryAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  GST_DEBUG_OBJECT (alloc, "init");

  alloc->mem_type = GST_ALLOCATOR_WRAPPED_MEMORY;

  alloc->mem_map = gst_wrapped_memory_map;
  alloc->mem_unmap = gst_wrapped_memory_unmap;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static void
gst_wrapped_memory_allocator_finalize (GObject * obj)
{
  GstWrappedMemoryAllocator *alloc = GST_WRAPPED_MEMORY_ALLOCATOR (obj);

  GST_DEBUG_OBJECT (alloc, "finalize");

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
wrapped_memory_allocator_class_init (GstWrappedMemoryAllocatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  gobject_class->finalize = gst_wrapped_memory_allocator_finalize;

  allocator_class->alloc = NULL;
  allocator_class->free = gst_wrapped_memory_allocator_free;
}

GstMemory *
gst_wrapped_memory_allocator_wrap (GstAllocator * allocator,
    void *data, gsize size, GFunc cb, gpointer user_data)
{
  GstMemory *mem = gst_wrapped_memory_allocator_memory_new (allocator, size);

  gst_wrapped_memory_allocator_memory_set_data (mem, data, cb, user_data);

  return mem;
}

GstMemory *
gst_wrapped_memory_allocator_memory_new (GstAllocator * allocator, gsize size)
{
  GstWrappedMemory *mem;

  if (!GST_IS_WRAPPED_MEMORY_ALLOCATOR (allocator)) {
    return NULL;
  }

  mem = g_slice_new0 (GstWrappedMemory);

  gst_memory_init (GST_MEMORY_CAST (mem),
      GST_MEMORY_FLAG_NO_SHARE | GST_MEMORY_FLAG_READONLY, allocator, NULL,
      size, 0, 0, size);

  GST_DEBUG_OBJECT (allocator, "alloc %p", mem);

  return GST_MEMORY_CAST (mem);
}

void
gst_wrapped_memory_allocator_memory_set_data (GstMemory * mem, gpointer data,
    GFunc cb, gpointer user_data)
{
  GstWrappedMemory *m;

  if (!gst_is_wrapped_memory_memory (mem)) {
    return;
  }

  m = (GstWrappedMemory *) mem;

  m->cb = cb;
  m->data = data;
  m->user_data = user_data;
}

void *
gst_wrapped_memory_get_data (GstMemory * mem)
{
  if (!gst_is_wrapped_memory_memory (mem)) {
    return NULL;
  }

  return ((GstWrappedMemory *) mem)->data;
}

gboolean
gst_is_wrapped_memory_memory (GstMemory * mem)
{
  return gst_memory_is_type (mem, GST_ALLOCATOR_WRAPPED_MEMORY);
}

static gpointer
gst_wrapped_memory_map (GstMemory * mem, G_GNUC_UNUSED gsize maxsize,
    GstMapFlags flags)
{
  GstWrappedMemory *m = (GstWrappedMemory *) mem;

  if (flags & GST_MAP_WRITE) {
    return NULL;
  }

  return m->data;
}

static void
gst_wrapped_memory_unmap (G_GNUC_UNUSED GstMemory * mem)
{
  /* nothing */
}

static void
gst_wrapped_memory_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstWrappedMemory *m = (GstWrappedMemory *) mem;
  GstWrappedMemoryAllocator *alloc = GST_WRAPPED_MEMORY_ALLOCATOR (allocator);

  GST_DEBUG_OBJECT (alloc, "free %p", m);

  if (m->cb) {
    m->cb (m->data, m->user_data);
  }

  g_slice_free (GstWrappedMemory, m);
}
