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

#include "gstdroidcamsrc.h"

#define gst_droidcamsrc_parent_class parent_class
G_DEFINE_TYPE (GstDroidCamSrc, gst_droidcamsrc, GST_TYPE_ELEMENT);

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

static void
gst_droidcamsrc_init (GstDroidCamSrc * src)
{
  src->hw = NULL;
  src->dev = NULL;
  src->vfsrc = NULL;
  src->imgsrc = NULL;
  src->vidsrc = NULL;
}

static void
gst_droidcamsrc_finalize (GObject * object)
{
  GstDroidCamSrc *src;

  src = GST_DROIDCAMSRC (object);

  GST_DEBUG_OBJECT (src, "finalize");

  // TODO:

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_droidcamsrc_fill_info (GstDroidCamSrc * src, GstDroidCamSrcCamInfo * target,
    int facing)
{
  struct camera_info info;
  int x;

  for (x = 0; x < MAX_CAMERAS; x++) {
    src->hw->get_camera_info (x, &info);

    if (info.facing == facing) {
      target->num = x;
      target->direction = info.facing;
      target->orientation = info.orientation / 90;

      GST_INFO_OBJECT (src, "camera %d is facing %d with orientation %d",
          target->num, target->direction, target->orientation);
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
gst_droid_cam_src_get_hw (GstDroidCamSrc * src)
{
  int err;
  int num;

  GST_DEBUG_OBJECT (src, "get hw");

  err =
      hw_get_module (CAMERA_HARDWARE_MODULE_ID,
      (const struct hw_module_t **) &src->hw);
  if (err < 0) {
    GST_ERROR_OBJECT (src, "error 0x%x getting camera hardware module", err);
    return FALSE;
  }

  if (src->hw->common.module_api_version > CAMERA_MODULE_API_VERSION_1_0) {
    GST_ERROR_OBJECT (src, "unsupported camera API version");
    src->hw = NULL;
    return FALSE;
  }

  num = src->hw->get_number_of_cameras ();
  if (num < 0) {
    GST_ERROR_OBJECT (src, "no camera hardware found");
    return FALSE;
  }

  if (num > MAX_CAMERAS) {
    GST_ERROR_OBJECT (src, "cannot support %d cameras", num);
    return FALSE;
  }

  src->info[0].num = src->info[1].num = -1;
  if (!gst_droidcamsrc_fill_info (src, &src->info[0], CAMERA_FACING_BACK)) {
    GST_WARNING_OBJECT (src, "cannot find back camera");
  }

  if (!gst_droidcamsrc_fill_info (src, &src->info[1], CAMERA_FACING_FRONT)) {
    GST_WARNING_OBJECT (src, "cannot find front camera");
  }

  return TRUE;
}

static GstStateChangeReturn
gst_droidcamsrc_change_state (GstElement * element, GstStateChange transition)
{
  GstDroidCamSrc *src;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  src = GST_DROIDCAMSRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_droid_cam_src_get_hw (src)) {
        ret = GST_STATE_CHANGE_FAILURE;
      }

      src->dev = gst_droidcamsrc_dev_new (src->hw);

      break;

    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_droidcamsrc_dev_open (src->dev, "0")) {
        ret = GST_STATE_CHANGE_FAILURE;
      }

      break;

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:

      break;

    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE) {
    return ret;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:

      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_droidcamsrc_dev_close (src->dev);
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_droidcamsrc_dev_destroy (src->dev);
      src->dev = NULL;
      src->hw = NULL;
      break;

    default:
      break;
  }

  return ret;
}

static void
gst_droidcamsrc_class_init (GstDroidCamSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Camera source", "Source/Video/Device",
      "Android HAL camera source", "Mohammed Sameer <msameer@foolab.org>");

  gobject_class->finalize = gst_droidcamsrc_finalize;
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_droidcamsrc_change_state);
}
