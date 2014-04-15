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

#ifndef __GST_DROID_CAM_SRC_BUFFER_POOL_H__
#define __GST_DROID_CAM_SRC_BUFFER_POOL_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_DROIDCAMSRC_BUFFER_POOL      (gst_droidcamsrc_buffer_pool_get_type())
#define GST_IS_DROIDCAMSRC_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DROIDCAMSRC_BUFFER_POOL))
#define GST_DROIDCAMSRC_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DROIDCAMSRC_BUFFER_POOL, GstDroidCamSrcBufferPool))
#define GST_DROIDCAMSRC_BUFFER_POOL_CAST(obj) ((GstDroidCamSrcBufferPool*)(obj))

#define GST_DROIDCAMSRC_BUFFER_POOL_USAGE_KEY  "usage"
#define GST_DROIDCAMSRC_BUFFER_POOL_WIDTH_KEY  "width"
#define GST_DROIDCAMSRC_BUFFER_POOL_HEIGHT_KEY "height"
#define GST_DROIDCAMSRC_BUFFER_POOL_FORMAT_KEY "format"
#define GST_DROIDCAMSRC_BUFFER_POOL_COUNT_KEY  "count"
#define GST_DROIDCAMSRC_BUFFER_POOL_LEFT_KEY   "left"
#define GST_DROIDCAMSRC_BUFFER_POOL_RIGHT_KEY  "right"
#define GST_DROIDCAMSRC_BUFFER_POOL_TOP_KEY    "top"
#define GST_DROIDCAMSRC_BUFFER_POOL_BOTTOM_KEY "bottom"

typedef struct _GstDroidCamSrcBufferPool GstDroidCamSrcBufferPool;
typedef struct _GstDroidCamSrcBufferPoolClass GstDroidCamSrcBufferPoolClass;
typedef struct _GstDroidCamSrcCamInfo GstDroidCamSrcCamInfo;

struct _GstDroidCamSrcBufferPool
{
  GstBufferPool parent;
  GstDroidCamSrcCamInfo *info;
};

struct _GstDroidCamSrcBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GstDroidCamSrcBufferPool * gst_droid_cam_src_buffer_pool_new (GstDroidCamSrcCamInfo * info);

G_END_DECLS

#endif /* __GST_DROID_CAM_SRC_BUFFER_POOL_H__ */
