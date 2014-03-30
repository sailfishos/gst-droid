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
#include "gstdroidcodecallocatoromx.h"

GST_DEBUG_CATEGORY_STATIC (droid_codec_omx_debug);
#define GST_CAT_DEFAULT droid_codec_omx_debug

typedef struct
{
  GstAllocator parent;

  GstDroidComponentPort *port;

} GstDroidCodecOmxAllocator;

typedef struct
{
  GstAllocatorClass parent_class;

} GstDroidCodecOmxAllocatorClass;

typedef struct
{
  GstMemory mem;

  OMX_BUFFERHEADERTYPE *omx_buf;

} GstDroidCodecOmxMemory;

#define omx_mem_allocator_parent_class parent_class
G_DEFINE_TYPE (GstDroidCodecOmxAllocator, omx_mem_allocator,
    GST_TYPE_ALLOCATOR);

#define GST_TYPE_OMX_ALLOCATOR    (omx_mem_allocator_get_type())
#define GST_OMX_ALLOCATOR(obj)    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OMX_ALLOCATOR,GstDroidCodecOmxAllocator))

GstAllocator *
gst_droid_codec_allocator_omx_new (GstDroidComponentPort * port)
{
  GstDroidCodecOmxAllocator *alloc;

  GST_DEBUG_CATEGORY_INIT (droid_codec_omx_debug, "droidcodecomx", 0,
      "droid codec omx allocator");

  alloc = g_object_new (GST_TYPE_OMX_ALLOCATOR, NULL);

  alloc->port = port;

  return GST_ALLOCATOR (alloc);
}

static gpointer
gst_omx_mem_mem_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstDroidCodecOmxMemory *omx_mem = (GstDroidCodecOmxMemory *) mem;
  return omx_mem->omx_buf->pBuffer + omx_mem->mem.offset;
}

static void
gst_omx_mem_mem_unmap (GstMemory * mem)
{
  /* nothing */
}

static void
omx_mem_allocator_init (GstDroidCodecOmxAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  GST_DEBUG_OBJECT (alloc, "init");

  alloc->mem_type = GST_ALLOCATOR_DROID_CODEC_OMX;

  alloc->mem_map = gst_omx_mem_mem_map;
  alloc->mem_unmap = gst_omx_mem_mem_unmap;
  alloc->mem_copy = NULL;
  alloc->mem_share = NULL;
  alloc->mem_is_span = NULL;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static void
gst_omx_allocator_finalize (GObject * obj)
{
  GstDroidCodecOmxAllocator *alloc = GST_OMX_ALLOCATOR (obj);

  GST_DEBUG_OBJECT (alloc, "finalize");

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_omx_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstDroidCodecOmxMemory *omx_mem;
  OMX_ERRORTYPE err;
  GstDroidCodecOmxAllocator *alloc = GST_OMX_ALLOCATOR (allocator);

  omx_mem = (GstDroidCodecOmxMemory *) mem;

  err = OMX_FreeBuffer (alloc->port->comp->omx,
      alloc->port->def.nPortIndex, omx_mem->omx_buf);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (alloc->port->comp->parent,
        "Failed to free buffer for port %li: %s (0x%08x)",
        alloc->port->def.nPortIndex, gst_omx_error_to_string (err), err);
  }

  g_slice_free (GstDroidCodecOmxMemory, omx_mem);
}

static GstMemory *
gst_omx_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstDroidCodecOmxMemory *mem;
  OMX_ERRORTYPE err;
  GstDroidCodecOmxAllocator *alloc = GST_OMX_ALLOCATOR (allocator);
  OMX_BUFFERHEADERTYPE *omx_buf = NULL;

  if (size != alloc->port->def.nBufferSize) {
    GST_ERROR_OBJECT (alloc->port->comp->parent,
        "invalid size passed %i vs requested %li", size,
        alloc->port->def.nBufferSize);
    return NULL;
  }

  mem = g_slice_new0 (GstDroidCodecOmxMemory);
  err = OMX_AllocateBuffer (alloc->port->comp->omx,
      &omx_buf, alloc->port->def.nPortIndex, mem, alloc->port->def.nBufferSize);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (alloc->port->comp->parent,
        "Failed to allocate buffer for port %li: %s (0x%08x)",
        alloc->port->def.nPortIndex, gst_omx_error_to_string (err), err);
    g_slice_free (GstDroidCodecOmxMemory, mem);

    return NULL;
  }

  mem->omx_buf = omx_buf;

  gst_memory_init (GST_MEMORY_CAST (mem), GST_MEMORY_FLAG_NO_SHARE, allocator,
      NULL, omx_buf->nAllocLen, alloc->port->def.nBufferAlignment, 0,
      omx_buf->nAllocLen);

  GST_DEBUG_OBJECT (alloc->port->comp->parent,
      "Allocated buffer for port %li", alloc->port->def.nPortIndex);

  return GST_MEMORY_CAST (mem);
}

static void
omx_mem_allocator_class_init (GstDroidCodecOmxAllocatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  gobject_class->finalize = gst_omx_allocator_finalize;

  allocator_class->alloc = gst_omx_allocator_alloc;
  allocator_class->free = gst_omx_allocator_free;
}

OMX_BUFFERHEADERTYPE *
gst_droid_omx_allocator_get_omx_buffer (GstMemory * mem)
{
  if (!gst_memory_is_type (mem, GST_ALLOCATOR_DROID_CODEC_OMX)) {
    return NULL;
  }

  return ((GstDroidCodecOmxMemory *) mem)->omx_buf;
}
