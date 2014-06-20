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
#include <EGL/egl.h>
#include <system/window.h>

#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

GST_DEBUG_CATEGORY_STATIC (droid_memory_debug);
#define GST_CAT_DEFAULT droid_memory_debug

typedef struct
{
  GstAllocator parent;

  EGLDisplay dpy;
  EGLContext ctx;

    EGLBoolean (*eglHybrisCreateNativeBuffer) (EGLint width, EGLint height,
      EGLint usage, EGLint format, EGLint * stride, EGLClientBuffer * buffer);
  void (*eglHybrisReleaseNativeBuffer) (EGLClientBuffer buffer);
    EGLBoolean (*eglHybrisLockNativeBuffer) (EGLClientBuffer buffer,
      EGLint usage, EGLint l, EGLint t, EGLint w, EGLint h, void **vaddr);
    EGLBoolean (*eglHybrisUnlockNativeBuffer) (EGLClientBuffer buffer);
} GstGrallocAllocator;

typedef struct
{
  GstAllocatorClass parent_class;

} GstGrallocAllocatorClass;

typedef struct
{
  GstMemory mem;

  struct ANativeWindowBuffer *remote;
  struct ANativeWindowBuffer buff;

} GstGrallocMemory;

#define gralloc_mem_allocator_parent_class parent_class
G_DEFINE_TYPE (GstGrallocAllocator, gralloc_mem_allocator, GST_TYPE_ALLOCATOR);

#define GST_TYPE_GRALLOC_ALLOCATOR    (gralloc_mem_allocator_get_type())
#define GST_IS_GRALLOC_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GRALLOC_ALLOCATOR))
#define GST_GRALLOC_ALLOCATOR(obj)    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GRALLOC_ALLOCATOR,GstGrallocAllocator))

static gboolean gst_gralloc_mem_is_span (GstMemory * mem1, GstMemory * mem2,
    gsize * offset);
static GstMemory *gst_gralloc_mem_copy (GstMemory * mem, gssize offset,
    gssize size);
static void gst_gralloc_allocator_free (GstAllocator * allocator,
    GstMemory * mem);

static void
incRef (struct android_native_base_t *base)
{
  struct ANativeWindowBuffer *self =
      container_of (base, struct ANativeWindowBuffer, common);

  GstGrallocMemory *mem = container_of (self, GstGrallocMemory, buff);

  gst_memory_ref (GST_MEMORY_CAST (mem));

  GST_DEBUG ("ref %p", mem);
}

static void
decRef (struct android_native_base_t *base)
{
  struct ANativeWindowBuffer *self =
      container_of (base, struct ANativeWindowBuffer, common);
  GstGrallocMemory *mem = container_of (self, GstGrallocMemory, buff);

  gst_memory_unref (GST_MEMORY_CAST (mem));

  GST_DEBUG ("unref %p", mem);
}

GstAllocator *
gst_gralloc_allocator_new (void)
{
  GST_DEBUG_CATEGORY_INIT (droid_memory_debug, "droidmemory", 0,
      "droid memory allocator");

  return g_object_new (GST_TYPE_GRALLOC_ALLOCATOR, NULL);
}

static void
gralloc_mem_allocator_init (GstGrallocAllocator * allocator)
{
  EGLConfig config;
  EGLint num_config;
  static EGLint const attribute_list[] = {
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_NONE
  };

  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  GST_DEBUG_OBJECT (alloc, "init");

  allocator->dpy = eglGetDisplay (EGL_DEFAULT_DISPLAY);

  if (!allocator->dpy) {
    GST_ERROR ("failed to get EGLDisplay");
    return;
  }

  if (!eglInitialize (allocator->dpy, NULL, NULL)) {
    GST_ERROR ("failed to initialize EGL");
    return;
  }

  if (!eglChooseConfig (allocator->dpy, attribute_list, &config, 1,
          &num_config)) {
    GST_ERROR ("failed to choose an EGL config");
    return;
  }

  if (num_config == 0) {
    GST_ERROR ("no valid EGL configurations");
    return;
  }

  allocator->ctx =
      eglCreateContext (allocator->dpy, config, EGL_NO_CONTEXT, NULL);

  if (allocator->ctx == EGL_NO_CONTEXT) {
    GST_ERROR ("failed to create an EGL context");
    return;
  }

  *(void **) &allocator->eglHybrisCreateNativeBuffer =
      eglGetProcAddress ("eglHybrisCreateNativeBuffer");

  *(void **) &allocator->eglHybrisReleaseNativeBuffer =
      eglGetProcAddress ("eglHybrisReleaseNativeBuffer");

  *(void **) &allocator->eglHybrisLockNativeBuffer =
      eglGetProcAddress ("eglHybrisUnlockNativeBuffer");

  *(void **) &allocator->eglHybrisUnlockNativeBuffer =
      eglGetProcAddress ("eglHybrisUnlockNativeBuffer");

  alloc->mem_type = GST_ALLOCATOR_GRALLOC;

  alloc->mem_map = NULL;
  alloc->mem_unmap = NULL;
  alloc->mem_copy = gst_gralloc_mem_copy;
  alloc->mem_share = NULL;
  alloc->mem_is_span = gst_gralloc_mem_is_span;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static void
gst_gralloc_allocator_finalize (GObject * obj)
{
  GstGrallocAllocator *alloc = GST_GRALLOC_ALLOCATOR (obj);

  GST_DEBUG_OBJECT (alloc, "finalize");

  if (alloc->ctx) {
    eglDestroyContext (alloc->dpy, alloc->ctx);
  }

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
  GstGrallocMemory *mem;
  int stride;

  if (!GST_IS_GRALLOC_ALLOCATOR (allocator)) {
    GST_WARNING ("it isn't the correct allocator for gralloc");
    return NULL;
  }

  alloc = GST_GRALLOC_ALLOCATOR (allocator);

  if (!alloc->dpy) {
    GST_ERROR ("no display");
    return NULL;
  }

  if (!alloc->ctx) {
    GST_ERROR ("no context");
    return NULL;
  }

  if (!alloc->eglHybrisCreateNativeBuffer) {
    GST_ERROR ("no eglHybrisCreateNativeBuffer");
    return NULL;
  }

  if (!alloc->eglHybrisReleaseNativeBuffer) {
    GST_ERROR ("no eglHybrisReleaseNativeBuffer");
    return NULL;
  }

  /* Now we are ready to serve */
  mem = g_slice_new0 (GstGrallocMemory);

  if (!alloc->eglHybrisCreateNativeBuffer (width, height, usage, format,
          &stride, (EGLClientBuffer *) & mem->remote)) {
    GST_ERROR ("Failed to allocate buffer");
    g_slice_free (GstGrallocMemory, mem);
    return NULL;
  }

  memset (mem->buff.common.reserved, 0, sizeof (mem->buff.common.reserved));

  mem->buff.width = mem->remote->width;
  mem->buff.height = mem->remote->height;
  mem->buff.stride = mem->remote->stride;
  mem->buff.format = mem->remote->format;
  mem->buff.usage = mem->remote->usage;
  mem->buff.handle = mem->remote->handle;
  mem->buff.common.magic = mem->remote->common.magic;
  mem->buff.common.version = mem->remote->common.version;

  mem->buff.common.incRef = incRef;
  mem->buff.common.decRef = decRef;

  gst_memory_init (GST_MEMORY_CAST (mem),
      GST_MEMORY_FLAG_NO_SHARE | GST_MEMORY_FLAG_NOT_MAPPABLE, allocator, NULL,
      -1, -1, 0, -1);

  GST_DEBUG_OBJECT (alloc, "alloc %p", mem);

  return GST_MEMORY_CAST (mem);
}

GstMemory *
gst_gralloc_allocator_wrap (GstAllocator * allocator, gint width, gint height,
    int usage, guint8 * data, gsize size, GstVideoFormat format)
{
  GstGrallocAllocator *alloc;
  EGLBoolean res;
  GstMemory *mem;
  void *addr = NULL;
  int hal_format = 0;
  int lock_usage =
      GST_GRALLOC_USAGE_SW_READ_NEVER | GST_GRALLOC_USAGE_SW_WRITE_RARELY;

  if (!GST_IS_GRALLOC_ALLOCATOR (allocator)) {
    return NULL;
  }

  alloc = GST_GRALLOC_ALLOCATOR (allocator);

  if (!alloc->eglHybrisLockNativeBuffer) {
    GST_ERROR ("no eglHybrisLockNativeBuffer");
    return NULL;
  }

  if (!alloc->eglHybrisUnlockNativeBuffer) {
    GST_ERROR ("no eglHybrisUnlockNativeBuffer");
    return NULL;
  }

  hal_format = gst_gralloc_gst_to_hal (format);

  if (hal_format == 0) {
    return NULL;
  }

  mem = gst_gralloc_allocator_alloc (allocator, width, height,
      hal_format, usage);

  if (!mem) {
    return NULL;
  }

  /* lock */
  res =
      alloc->eglHybrisLockNativeBuffer (((GstGrallocMemory *) mem)->remote,
      lock_usage, 0, 0, width, height, &addr);
  if (!res) {
    gst_memory_unref (mem);
    GST_ERROR ("failed to lock buffer");
    return NULL;
  }

  /* copy */
  memcpy (addr, data, size);

  /* unlock */
  res = alloc->eglHybrisUnlockNativeBuffer (((GstGrallocMemory *) mem)->remote);
  if (!res) {
    gst_memory_unref (mem);
    GST_ERROR ("failed to unlock buffer");
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

static GstMemory *
gst_gralloc_mem_copy (GstMemory * mem, gssize offset, gssize size)
{
  /* TODO: bad but playbin needs it
   * https://bugzilla.gnome.org/show_bug.cgi?id=727409
   */
  return gst_memory_ref (mem);
}

static void
gst_gralloc_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstGrallocAllocator *alloc = GST_GRALLOC_ALLOCATOR (allocator);

  GST_DEBUG_OBJECT (alloc, "free %p", mem);

  alloc->eglHybrisReleaseNativeBuffer (((GstGrallocMemory *) mem)->remote);

  g_slice_free (GstGrallocMemory, (GstGrallocMemory *) mem);
}

struct ANativeWindowBuffer *
gst_memory_get_native_buffer (GstMemory * mem)
{
  if (!gst_is_gralloc_memory (mem)) {
    return NULL;
  }

  return &((GstGrallocMemory *) mem)->buff;
}

GstVideoFormat
gst_gralloc_hal_to_gst (int hal)
{
  switch (hal) {
    case HAL_PIXEL_FORMAT_YV12:
      return GST_VIDEO_FORMAT_YV12;
    default:
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

int
gst_gralloc_gst_to_hal (GstVideoFormat gst)
{
  switch (gst) {
    case GST_VIDEO_FORMAT_YV12:
      return HAL_PIXEL_FORMAT_YV12;
      break;
    default:
      return 0;
  }
}
