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

#ifndef __GST_DROID_CAM_SRC_DEV_H__
#define __GST_DROID_CAM_SRC_DEV_H__

#include <gst/gst.h>
#include <hardware/camera.h>
#include "gstdroidcamsrcbufferpool.h"
#include "gstdroidcamsrcparams.h"
#include "gstdroidcamsrcstreamwindow.h"

G_BEGIN_DECLS

typedef struct _GstDroidCamSrcDev GstDroidCamSrcDev;
typedef struct _GstDroidCamSrcImageCaptureState GstDroidCamSrcImageCaptureState;
typedef struct _GstDroidCamSrcVideoCaptureState GstDroidCamSrcVideoCaptureState;
typedef struct _GstDroidCamSrcCamInfo GstDroidCamSrcCamInfo;

struct _GstDroidCamSrcDev
{
  camera_module_t *hw;
  camera_device_t *dev;
  GstDroidCamSrcParams *params;
  GstDroidCamSrcStreamWindow *win;
  GstDroidCamSrcPad *vfsrc;
  GstDroidCamSrcPad *imgsrc;
  GstDroidCamSrcPad *vidsrc;
  GstAllocator *allocator;
  GRecMutex *lock;

  GstDroidCamSrcCamInfo *info;
  GstDroidCamSrcImageCaptureState *img;
  GstDroidCamSrcVideoCaptureState *vid;
};

GstDroidCamSrcDev *gst_droidcamsrc_dev_new (camera_module_t *hw, GstDroidCamSrcPad *vfsrc,
					    GstDroidCamSrcPad *imgsrc,
					    GstDroidCamSrcPad *vidsrc, GRecMutex * lock);
void gst_droidcamsrc_dev_destroy (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_open (GstDroidCamSrcDev * dev, GstDroidCamSrcCamInfo * info);
void gst_droidcamsrc_dev_close (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_init (GstDroidCamSrcDev * dev);
void gst_droidcamsrc_dev_deinit (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_start (GstDroidCamSrcDev * dev, gboolean apply_settings);
void gst_droidcamsrc_dev_stop (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_set_params (GstDroidCamSrcDev * dev, const gchar *params);

gboolean gst_droidcamsrc_dev_capture_image (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_start_video_recording (GstDroidCamSrcDev * dev);
void gst_droidcamsrc_dev_stop_video_recording (GstDroidCamSrcDev * dev);

void gst_droidcamsrc_dev_update_params (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_start_autofocus (GstDroidCamSrcDev * dev);
void gst_droidcamsrc_dev_stop_autofocus (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_enable_face_detection (GstDroidCamSrcDev * dev, gboolean enable);

G_END_DECLS

#endif /* __GST_DROID_CAM_SRC_DEV_H__ */
