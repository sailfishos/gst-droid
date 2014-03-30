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
#include "gstdroidcodecallocatorgralloc.h"
#include "gst/memory/gstgralloc.h"
#include <android/system/window.h>

GST_DEBUG_CATEGORY_STATIC (droid_codec_gralloc_debug);
#define GST_CAT_DEFAULT droid_codec_gralloc_debug

typedef struct
{
  GstAllocator parent;

  GstDroidComponentPort *port;

  GstAllocator *gralloc;

} GstDroidCodecGrallocAllocator;

typedef struct
{
  GstAllocatorClass parent_class;

} GstDroidCodecGrallocAllocatorClass;

typedef struct
{
  GstMemory mem;

  GstMemory *gralloc;

  OMX_BUFFERHEADERTYPE *omx_buf;

} GstDroidCodecGrallocMemory;

#define gralloc_mem_allocator_parent_class parent_class
G_DEFINE_TYPE (GstDroidCodecGrallocAllocator, gralloc_mem_allocator,
    GST_TYPE_ALLOCATOR);

#define GST_TYPE_GRALLOC_ALLOCATOR    (gralloc_mem_allocator_get_type())
#define GST_GRALLOC_ALLOCATOR(obj)    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GRALLOC_ALLOCATOR,GstDroidCodecGrallocAllocator))

GstAllocator *
gst_droid_codec_allocator_gralloc_new (GstDroidComponentPort * port)
{
  GstDroidCodecGrallocAllocator *alloc;

  GST_DEBUG_CATEGORY_INIT (droid_codec_gralloc_debug, "droidcodecgralloc", 0,
      "droid codec gralloc allocator");

  alloc = g_object_new (GST_TYPE_GRALLOC_ALLOCATOR, NULL);

  alloc->port = port;

  return GST_ALLOCATOR (alloc);
}

static void
gralloc_mem_allocator_init (GstDroidCodecGrallocAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  GST_DEBUG_OBJECT (alloc, "init");

  alloc->mem_type = GST_ALLOCATOR_DROID_CODEC_GRALLOC;

  alloc->mem_map = NULL;
  alloc->mem_unmap = NULL;
  alloc->mem_copy = NULL;
  alloc->mem_share = NULL;
  alloc->mem_is_span = NULL;

  allocator->gralloc = gst_gralloc_allocator_new ();

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static void
gst_droid_codec_gralloc_allocator_finalize (GObject * obj)
{
  GstDroidCodecGrallocAllocator *alloc = GST_GRALLOC_ALLOCATOR (obj);

  GST_DEBUG_OBJECT (alloc, "finalize");

  gst_object_unref (alloc->gralloc);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_droid_codec_gralloc_allocator_free (GstAllocator * allocator,
    GstMemory * mem)
{
  GstDroidCodecGrallocMemory *omx_mem;
  OMX_ERRORTYPE err;
  GstDroidCodecGrallocAllocator *alloc = GST_GRALLOC_ALLOCATOR (allocator);

  omx_mem = (GstDroidCodecGrallocMemory *) mem;


  err = OMX_FreeBuffer (alloc->port->comp->omx,
      alloc->port->def.nPortIndex, omx_mem->omx_buf);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (alloc->port->comp->parent,
        "Failed to free buffer for port %li: %s (0x%08x)",
        alloc->port->def.nPortIndex, gst_omx_error_to_string (err), err);
  }

  gst_memory_unref (omx_mem->gralloc);

  g_slice_free (GstDroidCodecGrallocMemory, omx_mem);
}

static GstMemory *
gst_droid_codec_gralloc_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstMemory *gralloc;
  OMX_ERRORTYPE err;
  GstDroidCodecGrallocMemory *mem;
  struct ANativeWindowBuffer *native;
  GstDroidCodecGrallocAllocator *alloc = GST_GRALLOC_ALLOCATOR (allocator);
  OMX_BUFFERHEADERTYPE *omx_buf = NULL;

  if (size != alloc->port->def.nBufferSize) {
    GST_ERROR_OBJECT (alloc->port->comp->parent,
        "invalid size passed %i vs requested %li", size,
        alloc->port->def.nBufferSize);
    return NULL;
  }

  gralloc = gst_gralloc_allocator_alloc (alloc->gralloc,
      alloc->port->def.format.video.nFrameWidth,
      alloc->port->def.format.video.nFrameHeight,
      alloc->port->def.format.video.eColorFormat, alloc->port->usage);
  if (!gralloc) {
    GST_ERROR_OBJECT (alloc->port->comp->parent,
        "error allocating gralloc memory");
    return NULL;
  }

  mem = g_slice_new0 (GstDroidCodecGrallocMemory);
  native = gst_memory_get_native_buffer (gralloc);
  err =
      OMX_UseBuffer (alloc->port->comp->omx, &omx_buf,
      alloc->port->def.nPortIndex, mem, alloc->port->def.nBufferSize,
      (OMX_U8 *) native->handle);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (alloc->port->comp->parent,
        "Failed to use buffer for port %li: %s (0x%08x)",
        alloc->port->def.nPortIndex, gst_omx_error_to_string (err), err);
    gst_memory_unref (gralloc);
    g_slice_free (GstDroidCodecGrallocMemory, mem);
    return NULL;
  }

  mem->gralloc = gralloc;
  mem->omx_buf = omx_buf;

  gst_memory_init (GST_MEMORY_CAST (mem), GST_MEMORY_FLAG_NO_SHARE, allocator,
      NULL, omx_buf->nAllocLen, alloc->port->def.nBufferAlignment, 0,
      omx_buf->nAllocLen);

  GST_DEBUG_OBJECT (alloc->port->comp->parent,
      "Allocated buffer for port %li", alloc->port->def.nPortIndex);

  return GST_MEMORY_CAST (mem);
}

static void
gralloc_mem_allocator_class_init (GstDroidCodecGrallocAllocatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  gobject_class->finalize = gst_droid_codec_gralloc_allocator_finalize;

  allocator_class->alloc = gst_droid_codec_gralloc_allocator_alloc;
  allocator_class->free = gst_droid_codec_gralloc_allocator_free;
}

OMX_BUFFERHEADERTYPE *
gst_droid_codec_gralloc_allocator_get_omx_buffer (GstMemory * mem)
{
  if (!gst_memory_is_type (mem, GST_ALLOCATOR_DROID_CODEC_GRALLOC)) {
    return NULL;
  }

  return ((GstDroidCodecGrallocMemory *) mem)->omx_buf;
}
