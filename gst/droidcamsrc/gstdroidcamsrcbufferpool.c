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

#include "gstdroidcamsrcbufferpool.h"
#include "gst/memory/gstgralloc.h"
#include "gstdroidcamsrc.h"
#include <gst/meta/nemometa.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droid_camsrc_debug);
#define GST_CAT_DEFAULT gst_droid_camsrc_debug

#define gst_droidcamsrc_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstDroidCamSrcBufferPool, gst_droidcamsrc_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static void
gst_droidcamsrc_buffer_pool_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_droidcamsrc_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstMemory *mem;
  GstStructure *config;
  int width, height, format, usage;
  GstAllocator *allocator;
  GstDroidCamSrcBufferPool *pool = GST_DROIDCAMSRC_BUFFER_POOL (bpool);

  GST_INFO_OBJECT (pool, "alloc buffer");

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL (pool));
  if (!config) {
    GST_ERROR_OBJECT (pool, "failed to get buffer pool config");
    return GST_FLOW_ERROR;
  }

  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_WIDTH_KEY, &width);
  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_HEIGHT_KEY,
      &height);
  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_FORMAT_KEY,
      &format);
  gst_structure_get_int (config, GST_DROIDCAMSRC_BUFFER_POOL_USAGE_KEY, &usage);

  gst_buffer_pool_config_get_allocator (config, &allocator, NULL);

  gst_structure_free (config);

  usage |= GST_GRALLOC_USAGE_HW_TEXTURE;

  mem = gst_gralloc_allocator_alloc (allocator, width, height, format, usage);
  if (!mem) {
    GST_ERROR_OBJECT (pool, "failed to allocate gralloc memory");
    return GST_FLOW_ERROR;
  }

  *buffer = gst_buffer_new ();
  gst_buffer_append_memory (*buffer, mem);

  /* now our meta */
  if (!gst_buffer_add_gst_buffer_orientation_meta (*buffer,
          pool->info->orientation, pool->info->direction)) {
    gst_buffer_unref (*buffer);
    GST_ERROR_OBJECT (pool, "failed to add orientation meta");
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_add_video_crop_meta (*buffer)) {
    gst_buffer_unref (*buffer);
    GST_ERROR_OBJECT (pool, "failed to add crop meta");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

GstDroidCamSrcBufferPool *
gst_droid_cam_src_buffer_pool_new (GstDroidCamSrcCamInfo * info)
{
  GstDroidCamSrcBufferPool *pool =
      g_object_new (GST_TYPE_DROIDCAMSRC_BUFFER_POOL, NULL);
  pool->info = info;
  return pool;
}

static void
gst_droidcamsrc_buffer_pool_init (GstDroidCamSrcBufferPool * pool)
{
}

static void
gst_droidcamsrc_buffer_pool_class_init (GstDroidCamSrcBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_droidcamsrc_buffer_pool_finalize;
  gstbufferpool_class->alloc_buffer = gst_droidcamsrc_buffer_pool_alloc_buffer;
}
