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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include "gstgralloc.h"
#include "gstdroidmacros.h"
#include <hardware/gralloc.h>
#include <system/window.h>

GST_DEBUG_CATEGORY_STATIC (droid_memory_debug);
#define GST_CAT_DEFAULT droid_memory_debug

typedef struct
{
  GstAllocator parent;

  gralloc_module_t *gralloc;
  alloc_device_t *alloc;
  GMutex mutex;

} GstGrallocAllocator;

typedef struct
{
  GstAllocatorClass parent_class;

} GstGrallocAllocatorClass;

typedef struct
{
  GstMemory mem;

  struct ANativeWindowBuffer buff;

} GstGrallocMemory;

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (droid_memory_debug, "droidmemory", 0, \
      "droid memory allocator");

#define gralloc_mem_allocator_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGrallocAllocator, gralloc_mem_allocator, GST_TYPE_ALLOCATOR, _do_init);

#define GST_TYPE_GRALLOC_ALLOCATOR    (gralloc_mem_allocator_get_type())
#define GST_IS_GRALLOC_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GRALLOC_ALLOCATOR))
#define GST_GRALLOC_ALLOCATOR(obj)    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GRALLOC_ALLOCATOR,GstGrallocAllocator))

static gboolean gst_gralloc_mem_is_span (GstMemory * mem1, GstMemory * mem2,
    gsize * offset);
static void gst_gralloc_allocator_free (GstAllocator * allocator,
    GstMemory * mem);
static GstMemory * gst_gralloc_allocator_alloc (GstAllocator * allocator, gint width, gint height,
    int format, int usage);

static void
incRef (struct android_native_base_t *base)
{
  struct ANativeWindowBuffer *self =
      container_of (base, struct ANativeWindowBuffer, common);
  GstGrallocMemory *mem = container_of (self, GstGrallocMemory, buff);

  gst_memory_ref (GST_MEMORY_CAST (mem));
}

static void
decRef (struct android_native_base_t *base)
{
  struct ANativeWindowBuffer *self =
      container_of (base, struct ANativeWindowBuffer, common);
  GstGrallocMemory *mem = container_of (self, GstGrallocMemory, buff);

  gst_memory_unref (GST_MEMORY_CAST (mem));
}


GstAllocator *
gst_gralloc_allocator_new (void)
{
  return g_object_new (GST_TYPE_GRALLOC_ALLOCATOR, NULL);
}

static void
gralloc_mem_allocator_init (GstGrallocAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  GST_DEBUG_OBJECT (alloc, "init");

  allocator->gralloc = NULL;
  allocator->alloc = NULL;
  g_mutex_init (&allocator->mutex);
  alloc->mem_type = GST_ALLOCATOR_GRALLOC;

  alloc->mem_map = NULL;
  alloc->mem_unmap = NULL;
  alloc->mem_copy = NULL;
  alloc->mem_share = NULL;
  alloc->mem_is_span = gst_gralloc_mem_is_span;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static void
gst_gralloc_allocator_finalize (GObject * obj)
{
  GstGrallocAllocator *alloc = GST_GRALLOC_ALLOCATOR (obj);

  GST_DEBUG_OBJECT (alloc, "finalize");

  if (alloc->alloc) {
    gralloc_close (alloc->alloc);
  }

  g_mutex_clear (&alloc->mutex);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gralloc_mem_allocator_class_init (GstGrallocAllocatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  gobject_class->finalize = gst_gralloc_allocator_finalize;

  allocator_class->alloc = NULL;
  allocator_class->free = gst_gralloc_allocator_free;
}

GstMemory *
gst_gralloc_allocator_alloc (GstAllocator * allocator, gint width, gint height,
    int format, int usage)
{
  GstGrallocAllocator *alloc;
  int err;
  GstGrallocMemory *mem;
  int stride;

  if (!GST_IS_GRALLOC_ALLOCATOR (allocator)) {
    GST_WARNING ("it isn't the correct allocator for gralloc");
    return NULL;
  }

  alloc = GST_GRALLOC_ALLOCATOR (allocator);

  g_mutex_lock (&alloc->mutex);
  if (!alloc->gralloc) {
    err =
        hw_get_module (GRALLOC_HARDWARE_MODULE_ID,
        (const hw_module_t **) &alloc->gralloc);
    if (!alloc->gralloc) {
      GST_ERROR ("failed to initialize gralloc: %d", err);
      g_mutex_unlock (&alloc->mutex);
      return NULL;
    }

    err = gralloc_open ((const hw_module_t *) alloc->gralloc, &alloc->alloc);
    if (err) {
      GST_ERROR ("failed to open gralloc: %d", err);
      alloc->gralloc = NULL;
      alloc->alloc = NULL;
      g_mutex_unlock (&alloc->mutex);
      return NULL;
    }
  }

  /* Now we are ready to serve */
  mem = g_slice_new0 (GstGrallocMemory);

  err = alloc->alloc->alloc (alloc->alloc, width, height,
      format, usage, &mem->buff.handle, &stride);
  if (err) {
    GST_ERROR ("Failed to allocate buffer handle");
    g_slice_free (GstGrallocMemory, mem);
    g_mutex_unlock (&alloc->mutex);
    return NULL;
  }

  mem->buff.width = width;
  mem->buff.height = height;
  mem->buff.stride = stride;
  mem->buff.format = format;
  mem->buff.usage = usage;
  mem->buff.common.magic = ANDROID_NATIVE_BUFFER_MAGIC;
  mem->buff.common.version = sizeof (struct ANativeWindowBuffer);
  mem->buff.common.incRef = incRef;
  mem->buff.common.decRef = decRef;

  g_mutex_unlock (&alloc->mutex);

  gst_memory_init (GST_MEMORY_CAST (mem),
      GST_MEMORY_FLAG_NO_SHARE | GST_MEMORY_FLAG_NOT_MAPPABLE, allocator, NULL,
      0, 0, 0, 0);

  GST_DEBUG_OBJECT (alloc, "alloc %p", mem);

  return GST_MEMORY_CAST (mem);
}

GstMemory *
gst_gralloc_allocator_wrap (GstAllocator * allocator, GstVideoInfo info,
    guint8 * data, gsize size)
{
  GstGrallocAllocator *alloc;
  int err;
  void *addr = NULL;

  if (GST_VIDEO_FORMAT_INFO_FORMAT (info.finfo) != GST_VIDEO_FORMAT_YV12) {
    GST_ERROR_OBJECT (allocator, "only GST_VIDEO_FORMAT_YV12 is supported");
    return NULL;
  }

  if (!GST_IS_GRALLOC_ALLOCATOR (allocator)) {
    return NULL;
  }

  alloc = GST_GRALLOC_ALLOCATOR (allocator);

  GstMemory *mem = gst_gralloc_allocator_alloc (allocator, info.width, info.height,
      HAL_PIXEL_FORMAT_YV12, GRALLOC_USAGE_HW_TEXTURE);

  if (!mem) {
    return NULL;
  }

  /* lock */
  err =
      alloc->gralloc->lock (alloc->gralloc,
      ((GstGrallocMemory *) mem)->buff.handle,
      GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_RARELY, 0, 0,
      info.width, info.height, &addr);
  if (err != 0) {
    gst_memory_unref (mem);
    GST_ERROR ("failed to lock the buffer: %d", err);
    return NULL;
  }

  /* copy */
  memcpy (addr, data, size);

  /* unlock */
  err =
      alloc->gralloc->unlock (alloc->gralloc,
      ((GstGrallocMemory *) mem)->buff.handle);
  if (err != 0) {
    gst_memory_unref (mem);
    GST_ERROR ("failed to unlock the buffer: %d", err);
    return NULL;
  }

  GST_DEBUG_OBJECT (alloc, "wrapped %p of size %d in %p", data, size, mem);

  return mem;
}

gboolean
gst_is_gralloc_memory (GstMemory * mem)
{
  return gst_memory_is_type (mem, GST_ALLOCATOR_GRALLOC);
}

static gboolean
gst_gralloc_mem_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  return FALSE;
}

static void
gst_gralloc_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstGrallocMemory *m = (GstGrallocMemory *) mem;
  GstGrallocAllocator *alloc = GST_GRALLOC_ALLOCATOR (allocator);

  GST_DEBUG_OBJECT (alloc, "free %p", m);

  alloc->alloc->free (alloc->alloc, m->buff.handle);

  g_slice_free (GstGrallocMemory, m);
}

struct ANativeWindowBuffer *
gst_memory_get_native_buffer (GstMemory * mem)
{
  if (!gst_is_gralloc_memory (mem)) {
    return NULL;
  }

  return &((GstGrallocMemory *) mem)->buff;
}

GstMemory *
gst_memory_from_native_buffer (struct ANativeWindowBuffer * buffer)
{
  GstGrallocMemory *mem = container_of (buffer, GstGrallocMemory, buff);

  return GST_MEMORY_CAST (mem);
}
