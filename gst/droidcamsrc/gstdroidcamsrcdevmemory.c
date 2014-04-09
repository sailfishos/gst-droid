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

#include "gstdroidcamsrcdevmemory.h"
#include <unistd.h>
#include <sys/mman.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

typedef struct _GstDroidCamSrcDevMemory GstDroidCamSrcDevMemory;

struct _GstDroidCamSrcDevMemory
{
  int fd;
  unsigned int num_bufs;
  size_t buf_size;
};

static void
gst_droidcamsrc_dev_memory_release (struct camera_memory *mem)
{
  GstDroidCamSrcDevMemory *info;

  GST_DEBUG ("dev mem release");

  info = (GstDroidCamSrcDevMemory *) mem->handle;

  if (info->fd < 0) {
    g_free (mem->data);
  } else {
    if (munmap (mem->data, mem->size) == -1) {
      GST_ERROR ("munmap failed: %s", strerror (errno));
    }
  }

  g_slice_free (GstDroidCamSrcDevMemory, info);
  g_slice_free (camera_memory_t, mem);

  mem = NULL;
}

camera_memory_t *
gst_droidcamsrc_dev_memory_get (int fd, size_t buf_size, unsigned int num_bufs)
{
  camera_memory_t *mem;
  size_t total_size;
  size_t requested_size;
  size_t pagesize;
  void *mem_base;

  mem = g_slice_new0 (camera_memory_t);
  pagesize = getpagesize ();
  requested_size = buf_size * num_bufs;

  total_size = ((requested_size + pagesize - 1) & ~(pagesize - 1));

  if (requested_size != total_size) {
    GST_INFO ("adjusting size from %d to %d", requested_size, total_size);
    requested_size = total_size;
  }

  if (fd < 0) {
    mem_base = g_malloc (requested_size);
  } else {
    mem_base = mmap (0, requested_size,
        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem_base == MAP_FAILED) {
      GST_ERROR ("mmap failed: %s", strerror (errno));
      mem_base = NULL;
    }
  }

  if (mem_base == NULL) {
    GST_ERROR ("memory allocation failed");
    g_slice_free (camera_memory_t, mem);
    mem = NULL;
  } else {
    GstDroidCamSrcDevMemory *info = g_slice_new (GstDroidCamSrcDevMemory);
    info->fd = fd;
    info->num_bufs = num_bufs;
    info->buf_size = buf_size;

    mem->data = mem_base;
    mem->size = requested_size;
    mem->handle = info;
    mem->release = gst_droidcamsrc_dev_memory_release;
  }

  return mem;
}

void *
gst_droidcamsrc_dev_memory_get_data (const camera_memory_t * mem,
    unsigned int index, size_t * buf_size)
{
  GstDroidCamSrcDevMemory *info;
  size_t off;
  void *addr;

  if (!mem->handle) {
    return NULL;
  }

  info = (GstDroidCamSrcDevMemory *) mem->handle;

  if (index >= info->num_bufs) {
    return NULL;
  }

  *buf_size = info->buf_size;

  off = index * info->buf_size;
  addr = mem->data;
  addr += off;
  return addr;
}
