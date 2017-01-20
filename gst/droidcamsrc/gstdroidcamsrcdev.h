/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
 * Copyright (C) 2015-2016 Jolla LTD.
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

#ifndef __GST_DROIDCAMSRC_DEV_H__
#define __GST_DROIDCAMSRC_DEV_H__

#include <gst/gst.h>
#include "gstdroidcamsrcparams.h"
#include "droidmediacamera.h"
#include "droidmediaconstants.h"

G_BEGIN_DECLS

typedef struct _GstDroidCamSrcDev GstDroidCamSrcDev;
typedef struct _GstDroidCamSrcImageCaptureState GstDroidCamSrcImageCaptureState;
typedef struct _GstDroidCamSrcVideoCaptureState GstDroidCamSrcVideoCaptureState;
typedef struct _GstDroidCamSrcCamInfo GstDroidCamSrcCamInfo;
typedef struct _GstDroidCamSrcPad GstDroidCamSrcPad;
typedef struct _GstDroidCamSrcRecorder GstDroidCamSrcRecorder;

struct _GstDroidCamSrcDev
{
  DroidMediaCamera *cam;
  DroidMediaBufferQueue *queue;
  GstDroidCamSrcParams *params;
  GstDroidCamSrcPad *vfsrc;
  GstDroidCamSrcPad *imgsrc;
  GstDroidCamSrcPad *vidsrc;
  GstAllocator *wrap_allocator;
  GstAllocator *media_allocator;
  gboolean running;
  gboolean use_raw_data;
  GRecMutex *lock;
  GstDroidCamSrcCamInfo *info;
  GstDroidCamSrcImageCaptureState *img;
  GstDroidCamSrcVideoCaptureState *vid;
  GstBufferPool *pool;
  DroidMediaCameraConstants c;

  gboolean use_recorder;
  GstDroidCamSrcRecorder *recorder;
};

GstDroidCamSrcDev *gst_droidcamsrc_dev_new (GstDroidCamSrcPad *vfsrc,
					    GstDroidCamSrcPad *imgsrc,
					    GstDroidCamSrcPad *vidsrc, GRecMutex * lock);
void gst_droidcamsrc_dev_destroy (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_open (GstDroidCamSrcDev * dev, GstDroidCamSrcCamInfo * info);
void gst_droidcamsrc_dev_close (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_init (GstDroidCamSrcDev * dev);
void gst_droidcamsrc_dev_deinit (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_start (GstDroidCamSrcDev * dev, gboolean apply_settings);
void gst_droidcamsrc_dev_stop (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_set_params (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_capture_image (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_start_video_recording (GstDroidCamSrcDev * dev);
void gst_droidcamsrc_dev_stop_video_recording (GstDroidCamSrcDev * dev);

void gst_droidcamsrc_dev_update_params (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_start_autofocus (GstDroidCamSrcDev * dev);
void gst_droidcamsrc_dev_stop_autofocus (GstDroidCamSrcDev * dev);

gboolean gst_droidcamsrc_dev_enable_face_detection (GstDroidCamSrcDev * dev, gboolean enable);
gboolean gst_droidcamsrc_dev_restart (GstDroidCamSrcDev * dev);

void gst_droidcamsrc_dev_send_command (GstDroidCamSrcDev * dev, gint cmd, gint arg1, gint arg2);

gboolean gst_droidcamsrc_dev_is_running (GstDroidCamSrcDev * dev);

void gst_droidcamsrc_dev_queue_video_buffer (GstDroidCamSrcDev * dev, GstBuffer * buffer);

G_END_DECLS

#endif /* __GST_DROIDCAMSRC_DEV_H__ */
