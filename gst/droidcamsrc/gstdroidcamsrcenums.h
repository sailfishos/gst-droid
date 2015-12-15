/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
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

#ifndef __GST_DROIDCAMSRC_ENUMS_H__
#define __GST_DROIDCAMSRC_ENUMS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_DROIDCAMSRC_CAMERA_DEVICE (gst_droidcamsrc_camera_device_get_type())

typedef enum {
  GST_DROIDCAMSRC_CAMERA_DEVICE_PRIMARY = 0,
  GST_DROIDCAMSRC_CAMERA_DEVICE_SECONDARY = 1,
} GstDroidCamSrcCameraDevice;

GType gst_droidcamsrc_camera_device_get_type (void);

typedef enum {
  GST_DROIDCAMSRC_ROI_FOCUS_AREA = 0x1,
  GST_DROIDCAMSRC_ROI_METERING_AREA = 0x2,
  GST_DROIDCAMSRC_ROI_FACE_AREA = 0x4,
} GstDroidCamSrcRoiType;

G_END_DECLS

#endif /* __GST_DROIDCAMSRC_ENUMS_H__ */
