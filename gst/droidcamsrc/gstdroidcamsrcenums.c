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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstdroidcamsrcenums.h"

GType
gst_droidcamsrc_camera_device_get_type (void)
{
  static GType gst_droidcamsrc_camera_device_type = 0;
  static GEnumValue gst_droidcamsrc_camera_devices[] = {
    {GST_DROIDCAMSRC_CAMERA_DEVICE_PRIMARY, "Primary camera", "primary"},
    {GST_DROIDCAMSRC_CAMERA_DEVICE_SECONDARY, "Secondary camera", "secondary"},
    {0, NULL, NULL},
  };

  if (G_UNLIKELY (!gst_droidcamsrc_camera_device_type)) {
    gst_droidcamsrc_camera_device_type =
        g_enum_register_static ("GstDroidCamSrcCameraDevice",
        gst_droidcamsrc_camera_devices);
  }
  return gst_droidcamsrc_camera_device_type;
}

GType
gst_droidcamsrc_image_mode_get_type (void)
{
  static GType gst_droidcamsrc_image_mode_type = 0;
  static GFlagsValue gst_droidcamsrc_image_modes[] = {
    {GST_DROIDCAMSRC_IMAGE_MODE_NORMAL, "Normal image mode", "normal"},
    {GST_DROIDCAMSRC_IMAGE_MODE_ZSL, "ZSL image mode", "zsl"},
    {GST_DROIDCAMSRC_IMAGE_MODE_HDR, "HDR image mode", "hdr"},
    {0, NULL, NULL},
  };

  if (G_UNLIKELY (!gst_droidcamsrc_image_mode_type)) {
    gst_droidcamsrc_image_mode_type =
        g_flags_register_static ("GstDroidCamSrcImageMode",
        gst_droidcamsrc_image_modes);
  }
  return gst_droidcamsrc_image_mode_type;
}
