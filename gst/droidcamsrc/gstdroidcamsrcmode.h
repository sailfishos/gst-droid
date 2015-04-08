/*
 * gst-droid
 *
 * Copyright (C) 2015 Jolla LTD.
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

#ifndef __GST_DROID_CAM_SRC_MODE_H__
#define __GST_DROID_CAM_SRC_MODE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstDroidCamSrcMode GstDroidCamSrcMode;
typedef struct _GstDroidCamSrc GstDroidCamSrc;

struct _GstDroidCamSrcMode
{
  GstDroidCamSrc *src;
  GstPad *vfsrc;
  GstPad *modesrc;
};

GstDroidCamSrcMode *gst_droid_cam_src_mode_new_image (GstDroidCamSrc *src);
GstDroidCamSrcMode *gst_droid_cam_src_mode_new_video (GstDroidCamSrc *src);
void gst_droid_cam_src_mode_free (GstDroidCamSrcMode * mode);

gboolean gst_droid_cam_src_mode_activate (GstDroidCamSrcMode * mode);
void gst_droid_cam_src_mode_deactivate (GstDroidCamSrcMode * mode);

gboolean gst_droid_cam_src_mode_pad_is_significant (GstDroidCamSrcMode * mode, GstPad * pad);
gboolean gst_droid_cam_src_mode_negotiate (GstDroidCamSrcMode * mode, GstPad * pad);

G_END_DECLS

#endif /* __GST_DROID_CAM_SRC_MODE_H__ */
