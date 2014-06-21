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

#ifndef __GST_DROID_CAM_SRC_STREAM_WINDOW_H__
#define __GST_DROID_CAM_SRC_STREAM_WINDOW_H__

#include <gst/gst.h>
#include <hardware/camera.h>

G_BEGIN_DECLS

typedef struct _GstDroidCamSrcStreamWindow GstDroidCamSrcStreamWindow;
typedef struct _GstDroidCamSrcBufferPool GstDroidCamSrcBufferPool;
typedef struct _GstDroidCamSrcPad GstDroidCamSrcPad;
typedef struct _GstDroidCamSrcCamInfo GstDroidCamSrcCamInfo;

struct _GstDroidCamSrcStreamWindow
{
  preview_stream_ops_t window;
  int width;
  int height;
  int format;
  int count;
  int usage;
  gboolean needs_reconfigure;
  int top, left, bottom, right;
  GstDroidCamSrcBufferPool *pool;
  GMutex lock;
  GstDroidCamSrcPad *pad;
  GstAllocator *allocator;
  GstDroidCamSrcCamInfo *info;
};

GstDroidCamSrcStreamWindow * gst_droid_cam_src_stream_window_new (GstDroidCamSrcPad *pad,
								  GstDroidCamSrcCamInfo * info);
void gst_droid_cam_src_stream_window_destroy (GstDroidCamSrcStreamWindow * win);
void gst_droid_cam_src_stream_window_clear (GstDroidCamSrcStreamWindow * win);

G_END_DECLS

#endif /* __GST_DROID_CAM_SRC_STREAM_WINDOW_H__ */
