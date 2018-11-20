/*
 * gst-droid
 *
 * Copyright (C) 2015 Jolla LTD.
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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gst/gst.h>
#include "gstdroidmediabuffer.h"
#include "droidmediaconstants.h"

GST_DEBUG_CATEGORY_STATIC (droid_memory_debug);
#define GST_CAT_DEFAULT droid_memory_debug

typedef struct
{
  GstAllocator parent;

} GstDroidMediaBufferAllocator;

typedef struct
{
  GstAllocatorClass parent_class;

} GstDroidMediaBufferAllocatorClass;

typedef struct
{
  GstMemory mem;

  DroidMediaBuffer *buffer;
  GstVideoInfo video_info;
  gpointer map_data;
  int map_count;
  GstMapFlags map_flags;

} GstDroidMediaBufferMemory;

typedef struct
{
  int hal_format;
  GstVideoFormat gst_format;
  int bytes_per_pixel;
  int h_align;
  int v_align;

} GstDroidMediaBufferFormatMap;

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (droid_memory_debug, "droidmemory", 0, \
      "droid memory allocator");

G_DEFINE_TYPE_WITH_CODE (GstDroidMediaBufferAllocator,
    droid_media_buffer_allocator, GST_TYPE_ALLOCATOR, _do_init);

#define GST_TYPE_DROID_MEDIA_BUFFER_ALLOCATOR    (droid_media_buffer_allocator_get_type())
#define GST_IS_DROID_MEDIA_BUFFER_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DROID_MEDIA_BUFFER_ALLOCATOR))

static void gst_droid_media_buffer_allocator_free (GstAllocator * allocator,
    GstMemory * mem);

static gpointer gst_droid_media_buffer_memory_map (GstMemory * mem,
    gsize maxsize, GstMapFlags flags);
static void gst_droid_media_buffer_memory_unmap (GstMemory * mem);

#define GST_DROID_MEDIA_BUFFER_FORMAT_COUNT 11

static GstDroidMediaBufferFormatMap
    gst_droid_media_buffer_formats[GST_DROID_MEDIA_BUFFER_FORMAT_COUNT];

static void
gst_droid_media_buffer_initialize_format_map ()
{
  DroidMediaPixelFormatConstants constants;
  static gboolean initialized = FALSE;

  if (!initialized) {
    droid_media_pixel_format_constants_init (&constants);

    gst_droid_media_buffer_formats[0].hal_format =
        constants.HAL_PIXEL_FORMAT_RGBA_8888;
    gst_droid_media_buffer_formats[0].gst_format = GST_VIDEO_FORMAT_RGBA;
    gst_droid_media_buffer_formats[0].bytes_per_pixel = 4;
    gst_droid_media_buffer_formats[0].h_align = 4;
    gst_droid_media_buffer_formats[0].v_align = 1;

    gst_droid_media_buffer_formats[1].hal_format =
        constants.HAL_PIXEL_FORMAT_RGBX_8888;
    gst_droid_media_buffer_formats[1].gst_format = GST_VIDEO_FORMAT_RGBx;
    gst_droid_media_buffer_formats[1].bytes_per_pixel = 4;
    gst_droid_media_buffer_formats[1].h_align = 4;
    gst_droid_media_buffer_formats[1].v_align = 1;

    gst_droid_media_buffer_formats[2].hal_format =
        constants.HAL_PIXEL_FORMAT_RGB_888;
    gst_droid_media_buffer_formats[2].gst_format = GST_VIDEO_FORMAT_RGB;
    gst_droid_media_buffer_formats[2].bytes_per_pixel = 3;
    gst_droid_media_buffer_formats[2].h_align = 4;
    gst_droid_media_buffer_formats[2].v_align = 1;

    gst_droid_media_buffer_formats[3].hal_format =
        constants.HAL_PIXEL_FORMAT_RGB_565;
    gst_droid_media_buffer_formats[3].gst_format = GST_VIDEO_FORMAT_RGB16;
    gst_droid_media_buffer_formats[3].bytes_per_pixel = 2;
    gst_droid_media_buffer_formats[3].h_align = 4;
    gst_droid_media_buffer_formats[3].v_align = 1;

    gst_droid_media_buffer_formats[4].hal_format =
        constants.HAL_PIXEL_FORMAT_BGRA_8888;
    gst_droid_media_buffer_formats[4].gst_format = GST_VIDEO_FORMAT_BGRA;
    gst_droid_media_buffer_formats[4].bytes_per_pixel = 4;
    gst_droid_media_buffer_formats[4].h_align = 4;
    gst_droid_media_buffer_formats[4].v_align = 1;

    gst_droid_media_buffer_formats[5].hal_format =
        constants.HAL_PIXEL_FORMAT_YV12;
    gst_droid_media_buffer_formats[5].gst_format = GST_VIDEO_FORMAT_YV12;
    gst_droid_media_buffer_formats[5].bytes_per_pixel = 1;
    gst_droid_media_buffer_formats[5].h_align = 1;
    gst_droid_media_buffer_formats[5].v_align = 1;

    gst_droid_media_buffer_formats[6].hal_format =
        constants.HAL_PIXEL_FORMAT_YCbCr_422_SP;
    gst_droid_media_buffer_formats[6].gst_format = GST_VIDEO_FORMAT_NV16;
    gst_droid_media_buffer_formats[6].bytes_per_pixel = 1;
    gst_droid_media_buffer_formats[6].h_align = 1;
    gst_droid_media_buffer_formats[6].v_align = 1;

    gst_droid_media_buffer_formats[7].hal_format =
        constants.HAL_PIXEL_FORMAT_YCrCb_420_SP;
    gst_droid_media_buffer_formats[7].gst_format = GST_VIDEO_FORMAT_NV21;
    gst_droid_media_buffer_formats[7].bytes_per_pixel = 1;
    gst_droid_media_buffer_formats[7].h_align = 1;
    gst_droid_media_buffer_formats[7].v_align = 1;

    gst_droid_media_buffer_formats[8].hal_format =
        constants.HAL_PIXEL_FORMAT_YCbCr_422_I;
    gst_droid_media_buffer_formats[8].gst_format = GST_VIDEO_FORMAT_YUY2;
    gst_droid_media_buffer_formats[8].bytes_per_pixel = 1;
    gst_droid_media_buffer_formats[8].h_align = 1;
    gst_droid_media_buffer_formats[8].v_align = 1;

    gst_droid_media_buffer_formats[9].hal_format =
        constants.QOMX_COLOR_FormatYUV420PackedSemiPlanar32m;
    gst_droid_media_buffer_formats[9].gst_format = GST_VIDEO_FORMAT_YV12;
    gst_droid_media_buffer_formats[9].bytes_per_pixel = 1;
    gst_droid_media_buffer_formats[9].h_align = 128;
    gst_droid_media_buffer_formats[9].v_align = 32;

    gst_droid_media_buffer_formats[10].hal_format =
        constants.QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka;
    gst_droid_media_buffer_formats[10].gst_format = GST_VIDEO_FORMAT_NV12_64Z32;
    gst_droid_media_buffer_formats[10].bytes_per_pixel = 0;
    gst_droid_media_buffer_formats[10].h_align = 0;
    gst_droid_media_buffer_formats[10].v_align = 0;
  }
}

static int
gst_droid_media_buffer_index_of_hal_format (int format)
{
  int i;

  gst_droid_media_buffer_initialize_format_map ();

  for (i = 0; i < GST_DROID_MEDIA_BUFFER_FORMAT_COUNT; ++i) {
    if (gst_droid_media_buffer_formats[i].hal_format == format) {
      return i;
    }
  }
  return GST_DROID_MEDIA_BUFFER_FORMAT_COUNT;
}

static int
gst_droid_media_buffer_index_of_gst_format (GstVideoFormat format)
{
  int i;

  gst_droid_media_buffer_initialize_format_map ();

  for (i = 0; i < GST_DROID_MEDIA_BUFFER_FORMAT_COUNT; ++i) {
    if (gst_droid_media_buffer_formats[i].gst_format == format) {
      return i;
    }
  }
  return GST_DROID_MEDIA_BUFFER_FORMAT_COUNT;
}

GstAllocator *
gst_droid_media_buffer_allocator_new (void)
{
  return g_object_new (GST_TYPE_DROID_MEDIA_BUFFER_ALLOCATOR, NULL);
}

static void
droid_media_buffer_allocator_init (GstDroidMediaBufferAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  GST_DEBUG_OBJECT (alloc, "init");

  alloc->mem_type = GST_ALLOCATOR_DROID_MEDIA_BUFFER;

  alloc->mem_map = gst_droid_media_buffer_memory_map;
  alloc->mem_unmap = gst_droid_media_buffer_memory_unmap;
  alloc->mem_copy = NULL;
  alloc->mem_share = NULL;
  alloc->mem_is_span = NULL;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

#define ALIGN_SIZE(size, to) (((size) + to  - 1) & ~(to - 1))

static void
droid_media_buffer_allocator_class_init (GstDroidMediaBufferAllocatorClass *
    klass)
{
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = NULL;
  allocator_class->free = gst_droid_media_buffer_allocator_free;
}

static GstDroidMediaBufferMemory *
gst_droid_media_buffer_allocator_alloc_from_buffer (GstAllocator * allocator,
    DroidMediaBuffer * buffer, int format_index, gsize width, gsize height,
    gsize stride)
{
  GstDroidMediaBufferMemory *mem = g_slice_new0 (GstDroidMediaBufferMemory);
  GstFormat format;
  gsize padded_width = width;
  gsize padded_height = height;

  mem->buffer = buffer;
  mem->map_data = NULL;
  mem->map_flags = 0;
  mem->map_count = 0;

  if (format_index == GST_DROID_MEDIA_BUFFER_FORMAT_COUNT) {
    format = GST_VIDEO_FORMAT_ENCODED;
  } else {
    format = gst_droid_media_buffer_formats[format_index].gst_format;
    if (gst_droid_media_buffer_formats[format_index].bytes_per_pixel != 0) {
      padded_width =
          ALIGN_SIZE (stride,
          gst_droid_media_buffer_formats[format_index].h_align) /
          gst_droid_media_buffer_formats[format_index].bytes_per_pixel;
      padded_height =
          ALIGN_SIZE (height,
          gst_droid_media_buffer_formats[format_index].v_align);
    }
  }

  gst_video_info_set_format (&mem->video_info, format, padded_width,
      padded_height);
  mem->video_info.width = width;
  mem->video_info.height = height;

  gst_memory_init (GST_MEMORY_CAST (mem),
      GST_MEMORY_FLAG_NO_SHARE, allocator, NULL, mem->video_info.size, 0, 0,
      mem->video_info.size);

  return mem;
}

GstMemory *
gst_droid_media_buffer_allocator_alloc (GstAllocator * allocator,
    DroidMediaBufferQueue * queue, DroidMediaBufferCallbacks * cb)
{
  GstDroidMediaBufferMemory *mem;
  DroidMediaBufferInfo info;
  DroidMediaBuffer *buffer;
  int format_index;

  if (!GST_IS_DROID_MEDIA_BUFFER_ALLOCATOR (allocator)) {
    GST_WARNING_OBJECT (allocator,
        "allocator is not the correct allocator for droidmediabuffer");
    return NULL;
  }

  buffer = droid_media_buffer_queue_acquire_buffer (queue, cb);
  if (!buffer) {
    GST_ERROR_OBJECT (allocator, "failed to acquire media buffer");
    return NULL;
  }

  droid_media_buffer_get_info (buffer, &info);

  format_index = gst_droid_media_buffer_index_of_hal_format (info.format);

  mem = gst_droid_media_buffer_allocator_alloc_from_buffer (allocator, buffer,
      format_index, info.width, info.height, info.stride);

  GST_DEBUG_OBJECT (allocator, "alloc %p", mem);

  return GST_MEMORY_CAST (mem);
}

GstMemory *
gst_droid_media_buffer_allocator_alloc_from_data (GstAllocator * allocator,
    GstVideoInfo * info, DroidMediaData * data, DroidMediaBufferCallbacks * cb)
{
  GstDroidMediaBufferMemory *mem;
  DroidMediaBuffer *buffer;
  DroidMediaBufferInfo droid_info;
  int format_index;

  if (!GST_IS_DROID_MEDIA_BUFFER_ALLOCATOR (allocator)) {
    GST_WARNING_OBJECT (allocator,
        "allocator is not the correct allocator for droidmediabuffer");
    return NULL;
  }

  format_index =
      gst_droid_media_buffer_index_of_gst_format (info->finfo->format);

  if (format_index == GST_DROID_MEDIA_BUFFER_FORMAT_COUNT) {
    GST_WARNING_OBJECT (allocator,
        "Unknown GStreamer format %s",
        gst_video_format_to_string (info->finfo->format));
    return NULL;
  }

  buffer =
      droid_media_buffer_create_from_raw_data (info->width, info->height,
      GST_VIDEO_INFO_COMP_STRIDE (info, 0),
      GST_VIDEO_INFO_COMP_STRIDE (info, 1),
      gst_droid_media_buffer_formats[format_index].hal_format, data, cb);
  if (!buffer) {
    GST_ERROR_OBJECT (allocator, "failed to acquire media buffer");
    return NULL;
  }

  droid_media_buffer_get_info (buffer, &droid_info);

  mem = gst_droid_media_buffer_allocator_alloc_from_buffer (allocator, buffer,
      format_index, info->width, info->height, droid_info.stride);

  GST_DEBUG_OBJECT (allocator, "alloc %p", mem);

  return GST_MEMORY_CAST (mem);
}

gboolean
gst_is_droid_media_buffer_memory (GstMemory * mem)
{
  return gst_memory_is_type (mem, GST_ALLOCATOR_DROID_MEDIA_BUFFER);
}

static void
gst_droid_media_buffer_allocator_free (GstAllocator * allocator,
    GstMemory * mem)
{
  GstDroidMediaBufferMemory *m = (GstDroidMediaBufferMemory *) mem;

  GST_DEBUG_OBJECT (allocator, "free %p", m);

  droid_media_buffer_release (m->buffer, EGL_NO_DISPLAY, EGL_NO_SYNC_KHR);

  m->buffer = NULL;

  g_slice_free (GstDroidMediaBufferMemory, m);
}

DroidMediaBuffer *
gst_droid_media_buffer_memory_get_buffer (GstMemory * mem)
{
  if (!gst_is_droid_media_buffer_memory (mem)) {
    GST_ERROR ("memory %p is not droidmediabuffer memory", mem);
    return NULL;
  }

  return ((GstDroidMediaBufferMemory *) mem)->buffer;
}

gpointer
gst_droid_media_buffer_memory_map (GstMemory * mem, gsize maxsize,
    GstMapFlags flags)
{
  GstDroidMediaBufferMemory *m = (GstDroidMediaBufferMemory *) mem;
  int f = 0;

  (void) maxsize;

  if (flags & GST_MAP_READ) {
    f |= DROID_MEDIA_BUFFER_LOCK_READ;
  }
  if (flags & GST_MAP_WRITE) {
    f |= DROID_MEDIA_BUFFER_LOCK_WRITE;
  }

  if (m->map_count > 0) {
    if (m->map_flags != f) {
      GST_ERROR ("Tried to lock buffer with different flags");
      return NULL;
    }
  } else {
    m->map_data = droid_media_buffer_lock (m->buffer, f);

    if (!m->map_data) {
      GST_ERROR ("Tried to lock buffer with different flags");
      return NULL;
    }
  }

  m->map_flags = f;
  m->map_count += 1;

  return m->map_data;
}

void
gst_droid_media_buffer_memory_unmap (GstMemory * mem)
{
  GstDroidMediaBufferMemory *m = (GstDroidMediaBufferMemory *) mem;

  if (m->map_count > 0 && (m->map_count -= 1) == 0) {
    m->map_data = NULL;
    droid_media_buffer_unlock (m->buffer);
  }
}

GstVideoInfo *
gst_droid_media_buffer_get_video_info (GstMemory * mem)
{
  if (!gst_is_droid_media_buffer_memory (mem)) {
    GST_ERROR ("memory %p is not droidmediabuffer memory", mem);
    return NULL;
  }

  return &((GstDroidMediaBufferMemory *) mem)->video_info;
}
