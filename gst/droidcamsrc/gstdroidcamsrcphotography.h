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

#ifndef __GST_DROIDCAMSRC_PHOTOGRAPHY_H__
#define __GST_DROIDCAMSRC_PHOTOGRAPHY_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstDroidCamSrc GstDroidCamSrc;
typedef enum _GstDroidCamSrcApplyType GstDroidCamSrcApplyType;

typedef enum
{
  PROP_0,
  PROP_DEVICE_PARAMETERS,
  PROP_CAMERA_DEVICE,
  PROP_MODE,
  PROP_IMAGE_MODE,
  PROP_SUPPORTED_IMAGE_MODES,
  PROP_READY_FOR_CAPTURE,
  PROP_MAX_ZOOM,
  PROP_VIDEO_TORCH,
  PROP_MIN_EV_COMPENSATION,
  PROP_MAX_EV_COMPENSATION,
  PROP_FACE_DETECTION,
  PROP_IMAGE_NOISE_REDUCTION,
  PROP_SENSOR_ORIENTATION,
  PROP_SENSOR_MOUNT_ANGLE,
  PROP_TARGET_BITRATE,

  /* photography interface */
  PROP_WB_MODE,
  PROP_COLOR_TONE,
  PROP_SCENE_MODE,
  PROP_FLASH_MODE,
  PROP_FLICKER_MODE,
  PROP_FOCUS_MODE,
  PROP_CAPABILITIES,
  PROP_EV_COMP,
  PROP_ISO_SPEED,
  PROP_APERTURE,
  PROP_EXPOSURE_TIME,
  PROP_IMAGE_CAPTURE_SUPPORTED_CAPS,
  PROP_IMAGE_PREVIEW_SUPPORTED_CAPS,
  PROP_ZOOM,
  PROP_COLOR_TEMPERATURE,
  PROP_WHITE_POINT,
  PROP_ANALOG_GAIN,
  PROP_LENS_FOCUS,
  PROP_MIN_EXPOSURE_TIME,
  PROP_MAX_EXPOSURE_TIME,
  PROP_NOISE_REDUCTION,
  PROP_EXPOSURE_MODE,
} GstDroidCamSrcProperties;

void gst_droidcamsrc_photography_register (gpointer g_iface,  gpointer iface_data);
void gst_droidcamsrc_photography_add_overrides (GObjectClass * klass);
void gst_droidcamsrc_photography_init (GstDroidCamSrc * src, gint dev);
void gst_droidcamsrc_photography_destroy (GstDroidCamSrc * src);
gboolean gst_droidcamsrc_photography_get_property (GstDroidCamSrc * src, guint prop_id,
						   GValue * value, GParamSpec * pspec);
gboolean gst_droidcamsrc_photography_set_property (GstDroidCamSrc * src, guint prop_id,
						   const GValue * value, GParamSpec * pspec);
void gst_droidcamsrc_photography_apply (GstDroidCamSrc * src,
					GstDroidCamSrcApplyType type);
void gst_droidcamsrc_photography_set_focus_to_droid (GstDroidCamSrc * src);
void gst_droidcamsrc_photography_set_flash_to_droid (GstDroidCamSrc * src);

G_END_DECLS

#endif /* __GST_DROIDCAMSRC_PHOTOGRAPHY_H__ */
