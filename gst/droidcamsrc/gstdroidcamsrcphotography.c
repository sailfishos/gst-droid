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

#include "gstdroidcamsrcphotography.h"
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include <gst/interfaces/photography.h>

void
gst_droidcamsrc_photography_init (gpointer g_iface, gpointer iface_data)
{
  // TODO:

}

void
gst_droidcamsrc_photography_override (GObjectClass * klass)
{
  g_object_class_override_property (klass, PROP_WB_MODE,
      GST_PHOTOGRAPHY_PROP_WB_MODE);
  g_object_class_override_property (klass, PROP_COLOR_TONE,
      GST_PHOTOGRAPHY_PROP_COLOR_TONE);
  g_object_class_override_property (klass, PROP_SCENE_MODE,
      GST_PHOTOGRAPHY_PROP_SCENE_MODE);
  g_object_class_override_property (klass, PROP_FLASH_MODE,
      GST_PHOTOGRAPHY_PROP_FLASH_MODE);
  g_object_class_override_property (klass, PROP_FLICKER_MODE,
      GST_PHOTOGRAPHY_PROP_FLICKER_MODE);
  g_object_class_override_property (klass, PROP_FOCUS_MODE,
      GST_PHOTOGRAPHY_PROP_FOCUS_MODE);
  g_object_class_override_property (klass, PROP_CAPABILITIES,
      GST_PHOTOGRAPHY_PROP_CAPABILITIES);
  g_object_class_override_property (klass, PROP_EV_COMP,
      GST_PHOTOGRAPHY_PROP_EV_COMP);
  g_object_class_override_property (klass, PROP_ISO_SPEED,
      GST_PHOTOGRAPHY_PROP_ISO_SPEED);
  g_object_class_override_property (klass, PROP_APERTURE,
      GST_PHOTOGRAPHY_PROP_APERTURE);
  g_object_class_override_property (klass, PROP_EXPOSURE_TIME,
      GST_PHOTOGRAPHY_PROP_EXPOSURE_TIME);
  g_object_class_override_property (klass, PROP_IMAGE_CAPTURE_SUPPORTED_CAPS,
      GST_PHOTOGRAPHY_PROP_IMAGE_CAPTURE_SUPPORTED_CAPS);
  g_object_class_override_property (klass, PROP_IMAGE_PREVIEW_SUPPORTED_CAPS,
      GST_PHOTOGRAPHY_PROP_IMAGE_PREVIEW_SUPPORTED_CAPS);
  g_object_class_override_property (klass, PROP_ZOOM,
      GST_PHOTOGRAPHY_PROP_ZOOM);
  g_object_class_override_property (klass, PROP_COLOR_TEMPERATURE,
      GST_PHOTOGRAPHY_PROP_COLOR_TEMPERATURE);
  g_object_class_override_property (klass, PROP_WHITE_POINT,
      GST_PHOTOGRAPHY_PROP_WHITE_POINT);
  g_object_class_override_property (klass, PROP_ANALOG_GAIN,
      GST_PHOTOGRAPHY_PROP_ANALOG_GAIN);
  g_object_class_override_property (klass, PROP_LENS_FOCUS,
      GST_PHOTOGRAPHY_PROP_LENS_FOCUS);
  g_object_class_override_property (klass, PROP_MIN_EXPOSURE_TIME,
      GST_PHOTOGRAPHY_PROP_MIN_EXPOSURE_TIME);
  g_object_class_override_property (klass, PROP_MAX_EXPOSURE_TIME,
      GST_PHOTOGRAPHY_PROP_MAX_EXPOSURE_TIME);
  g_object_class_override_property (klass, PROP_NOISE_REDUCTION,
      GST_PHOTOGRAPHY_PROP_NOISE_REDUCTION);
}

gboolean
gst_droidcamsrc_photography_get_property (GstDroidCamSrc * src, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  // TODO:

  switch (prop_id) {
    case PROP_WB_MODE:
    case PROP_COLOR_TONE:
    case PROP_SCENE_MODE:
    case PROP_FLASH_MODE:
    case PROP_FLICKER_MODE:
    case PROP_FOCUS_MODE:
    case PROP_CAPABILITIES:
    case PROP_EV_COMP:
    case PROP_ISO_SPEED:
    case PROP_APERTURE:
    case PROP_EXPOSURE_TIME:
    case PROP_IMAGE_CAPTURE_SUPPORTED_CAPS:
    case PROP_IMAGE_PREVIEW_SUPPORTED_CAPS:
    case PROP_ZOOM:
    case PROP_COLOR_TEMPERATURE:
    case PROP_WHITE_POINT:
    case PROP_ANALOG_GAIN:
    case PROP_LENS_FOCUS:
    case PROP_MIN_EXPOSURE_TIME:
    case PROP_MAX_EXPOSURE_TIME:
    case PROP_NOISE_REDUCTION:
      return TRUE;
  }

  return FALSE;
}

gboolean
gst_droidcamsrc_photography_set_property (GstDroidCamSrc * src, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  // TODO:

  switch (prop_id) {
    case PROP_WB_MODE:
    case PROP_COLOR_TONE:
    case PROP_SCENE_MODE:
    case PROP_FLASH_MODE:
    case PROP_FLICKER_MODE:
    case PROP_FOCUS_MODE:
    case PROP_EV_COMP:
    case PROP_ISO_SPEED:
    case PROP_APERTURE:
    case PROP_EXPOSURE_TIME:
    case PROP_ZOOM:
    case PROP_COLOR_TEMPERATURE:
    case PROP_WHITE_POINT:
    case PROP_ANALOG_GAIN:
    case PROP_LENS_FOCUS:
    case PROP_MIN_EXPOSURE_TIME:
    case PROP_MAX_EXPOSURE_TIME:
    case PROP_NOISE_REDUCTION:
      return TRUE;
  }

  return FALSE;
}
