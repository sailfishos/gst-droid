/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
 * Copyright (C) 2016 Jolla LTD.
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

#ifndef __GST_DROIDCAMSRC_H__
#define __GST_DROIDCAMSRC_H__

#include <gst/gst.h>
#include "gstdroidcamsrcdev.h"
#include "gstdroidcamsrcenums.h"
#include "gstdroidcamsrcquirks.h"
#include <gst/meta/nemometa.h>
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include <gst/basecamerabinsrc/gstcamerabin-enum.h>
#include <gst/basecamerabinsrc/gstbasecamerasrc.h>
#include "droidmediacamera.h"
#include "gstdroidcamsrcmode.h"

G_BEGIN_DECLS

#define GST_TYPE_DROIDCAMSRC \
  (gst_droidcamsrc_get_type())
#define GST_DROIDCAMSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DROIDCAMSRC, GstDroidCamSrc))
#define GST_DROIDCAMSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DROIDCAMSRC, GstDroidCamSrcClass))
#define GST_IS_DROIDCAMSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DROIDCAMSRC))
#define GST_IS_DROIDCAMSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DROIDCAMSRC))

#define MAX_CAMERAS 2
#define GST_DROIDCAMSRC_CAPTURE_START "photo-capture-start"
#define GST_DROIDCAMSRC_CAPTURE_END "photo-capture-end"
#define GST_DROIDCAMSRC_PREVIEW_IMAGE "photo-capture-preview"

typedef struct _GstDroidCamSrc GstDroidCamSrc;
typedef struct _GstDroidCamSrcClass GstDroidCamSrcClass;
typedef struct _GstDroidCamSrcCamInfo GstDroidCamSrcCamInfo;
typedef struct _GstDroidCamSrcPad GstDroidCamSrcPad;
typedef struct _GstDroidCamSrcPhotography GstDroidCamSrcPhotography;
typedef enum _GstDroidCamSrcApplyType GstDroidCamSrcApplyType;

typedef gboolean (* GstDroidCamSrcNegotiateCallback)(GstDroidCamSrcPad * pad);

struct _GstDroidCamSrcCamInfo
{
  int num;
  NemoGstDeviceDirection direction;
  NemoGstBufferOrientation orientation;
};

struct _GstDroidCamSrcPad
{
  GstPad *pad;
  GQueue *queue;
  GCond cond;
  GMutex lock;
  gboolean running;
  gboolean open_stream;
  gboolean open_segment;
  gboolean adjust_segment;
  gboolean capture_pad;
  unsigned pushed_buffers;
  GstSegment segment;
  GstDroidCamSrcNegotiateCallback negotiate;
  GList *pending_events;
};

struct _GstDroidCamSrc
{
  GstElement parent;

  GstDroidCamSrcQuirks *quirks;
  GstDroidCamSrcDev *dev;
  GRecMutex dev_lock;
  GstDroidCamSrcCamInfo info[MAX_CAMERAS];

  GstDroidCamSrcPad *vfsrc;
  GstDroidCamSrcPad *imgsrc;
  GstDroidCamSrcPad *vidsrc;

  GstDroidCamSrcMode *image;
  GstDroidCamSrcMode *video;
  GstDroidCamSrcMode *active_mode;

  GstDroidCamSrcCameraDevice camera_device;
  GstCameraBinMode mode;

  int captures;
  GMutex capture_lock;

  gboolean video_torch;
  gboolean face_detection;
  gboolean image_noise_reduction;
  GstDroidCamSrcImageMode image_mode;

  GstDroidCamSrcPhotography * photo;
  gfloat max_zoom;
  gfloat min_ev_compensation;
  gfloat max_ev_compensation;
  gfloat ev_step;

  gint32 target_bitrate;

  /* protected with OBJECT_LOCK */
  gint width;
  gint height;
  gint fps_n, fps_d;
  DroidMediaRect crop_rect;
};

struct _GstDroidCamSrcClass
{
  GstElementClass parent_class;
};

enum _GstDroidCamSrcApplyType
{
  SET_ONLY,
  SET_AND_APPLY,
};

GType gst_droidcamsrc_get_type (void);
void gst_droidcamsrc_post_message (GstDroidCamSrc * src, GstStructure * s);
void gst_droidcamsrc_timestamp (GstDroidCamSrc * src, GstBuffer * buffer);
gboolean gst_droidcamsrc_apply_params (GstDroidCamSrc * src);
void gst_droidcamsrc_apply_mode_settings (GstDroidCamSrc * src, GstDroidCamSrcApplyType type);
void gst_droidcamsrc_update_max_zoom (GstDroidCamSrc * src);

G_END_DECLS

#endif /* __GST_DROIDCAMSRC_H__ */
