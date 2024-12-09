/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer
 * Copyright (C) 2015-2021 Jolla Ltd.
 * Copyright (C) 2010 Texas Instruments, Inc
 * Copyright (C) 2021-2022 Open Mobile Platform LLC.
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

#include "plugin.h"
#include "gstdroidcamsrc.h"
#include "gstdroidcamsrcquirks.h"
#include <gst/video/video.h>
#include "gst/droid/gstdroidmediabuffer.h"
#include "gst/droid/gstdroidbufferpool.h"
#include "gst/droid/gstwrappedmemory.h"
#include "gst/droid/gstdroidquery.h"
#include "gst/droid/gstdroidcodec.h"
#include "gstdroidcamsrcphotography.h"
#include "gstdroidcamsrcrecorder.h"
#include "droidmediacamera.h"
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include <gst/interfaces/photography.h>

#define gst_droidcamsrc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDroidCamSrc, gst_droidcamsrc, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_PHOTOGRAPHY,
        gst_droidcamsrc_photography_register));

GST_DEBUG_CATEGORY_EXTERN (gst_droid_camsrc_debug);
#define GST_CAT_DEFAULT gst_droid_camsrc_debug

static GstStaticPadTemplate vf_src_template_factory =
    GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_QUEUE_BUFFER,
            GST_DROID_MEDIA_BUFFER_MEMORY_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE ("{NV21}")));

static GstStaticPadTemplate img_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg"));

#if 0
static GstStaticPadTemplate vid_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_VIDEO_META_DATA, "{YV12}")));
#endif

static gboolean gst_droidcamsrc_pad_activate_mode (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active);
static gboolean gst_droidcamsrc_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_droidcamsrc_pad_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_droidcamsrc_vfsrc_negotiate (GstDroidCamSrcPad * data);
static gboolean gst_droidcamsrc_imgsrc_negotiate (GstDroidCamSrcPad * data);
static gboolean gst_droidcamsrc_vidsrc_negotiate (GstDroidCamSrcPad * data);
static void gst_droidcamsrc_start_capture (GstDroidCamSrc * src);
static void gst_droidcamsrc_stop_capture (GstDroidCamSrc * src);
static void gst_droidcamsrc_update_ev_compensation_bounds (GstDroidCamSrc *
    src);
static void gst_droidcamsrc_add_vfsrc_orientation_tag (GstDroidCamSrc * src);
static gboolean gst_droidcamsrc_select_and_activate_mode (GstDroidCamSrc * src);
static GstCaps *gst_droidcamsrc_pick_largest_resolution (GstDroidCamSrc * src,
    GstCaps * caps);
static gchar *gst_droidcamsrc_find_picture_resolution (GstDroidCamSrc * src,
    const gchar * resolution);
static gboolean gst_droidcamsrc_is_zsl_and_hdr_supported (GstDroidCamSrc * src);
static GstCaps *gst_droidcamsrc_get_video_caps_locked (GstDroidCamSrc * src);
static gboolean gst_droidcamsrc_get_hw (GstDroidCamSrc * src);
static gboolean gst_droidcamsrc_update_jpeg_quality(GstDroidCamSrc * src);

enum
{
  /* action signals */
  START_CAPTURE_SIGNAL,
  STOP_CAPTURE_SIGNAL,
  /* emit signals */
  LAST_SIGNAL
};

static guint droidcamsrc_signals[LAST_SIGNAL];

#define DEFAULT_CAMERA_DEVICE          0
#define DEFAULT_MODE                   MODE_IMAGE
#define DEFAULT_MAX_ZOOM               10.0f
#define DEFAULT_VIDEO_TORCH            FALSE
#define DEFAULT_MIN_EV_COMPENSATION    -2.5f
#define DEFAULT_MAX_EV_COMPENSATION    2.5f
#define DEFAULT_FACE_DETECTION         FALSE
#define DEFAULT_IMAGE_NOISE_REDUCTION  TRUE
#define DEFAULT_SENSOR_DIRECTION       0
#define DEFAULT_SENSOR_ORIENTATION     0
#define DEFAULT_IMAGE_MODE             GST_DROIDCAMSRC_IMAGE_MODE_NORMAL
#define DEFAULT_TARGET_BITRATE         12000000
#define DEFAULT_POST_PREVIEW           FALSE
/* JPEG quality for photos in %, min 10% max 100% - safe values */
#define DEFAULT_JPEG_QUALITY           90
#define DEFAULT_MIN_JPEG_QUALITY       10
#define DEFAULT_MAX_JPEG_QUALITY       100

static GstDroidCamSrcPad *
gst_droidcamsrc_create_pad (GstDroidCamSrc * src,
    const gchar * name, gboolean capture_pad)
{
  GstDroidCamSrcPad *pad = g_slice_new0 (GstDroidCamSrcPad);
  GstPadTemplate *tpl =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (src), name);
  pad->pad = gst_pad_new_from_template (tpl, name);

  /* TODO: I don't think this is needed */
  gst_pad_use_fixed_caps (pad->pad);
  gst_pad_set_element_private (pad->pad, pad);

  gst_pad_set_activatemode_function (pad->pad,
      gst_droidcamsrc_pad_activate_mode);
  gst_pad_set_event_function (pad->pad, gst_droidcamsrc_pad_event);
  gst_pad_set_query_function (pad->pad, gst_droidcamsrc_pad_query);

  g_mutex_init (&pad->lock);
  g_cond_init (&pad->cond);
  pad->queue = g_queue_new ();
  pad->running = FALSE;
  pad->negotiate = NULL;
  pad->capture_pad = capture_pad;
  pad->pushed_buffers = 0;
  pad->adjust_segment = FALSE;
  pad->pending_events = NULL;
  gst_segment_init (&pad->segment, GST_FORMAT_TIME);

  gst_element_add_pad (GST_ELEMENT (src), pad->pad);

  return pad;
}

static void
gst_droidcamsrc_destroy_pad (GstDroidCamSrcPad * pad)
{
  /* we don't destroy the pad itself */
  g_mutex_clear (&pad->lock);
  g_cond_clear (&pad->cond);
  g_queue_free (pad->queue);
  g_slice_free (GstDroidCamSrcPad, pad);
}

static void
gst_droidcamsrc_init (GstDroidCamSrc * src)
{
  src->quirks = gst_droidcamsrc_quirks_new ();
  g_rec_mutex_init (&src->dev_lock);
  src->dev = NULL;
  src->info = NULL;
  src->camera_device = DEFAULT_CAMERA_DEVICE;
  src->mode = DEFAULT_MODE;
  src->captures = 0;
  g_mutex_init (&src->capture_lock);
  src->max_zoom = DEFAULT_MAX_ZOOM;
  src->video_torch = DEFAULT_VIDEO_TORCH;
  src->face_detection = DEFAULT_FACE_DETECTION;
  src->image_noise_reduction = DEFAULT_IMAGE_NOISE_REDUCTION;
  src->image_mode = GST_DROIDCAMSRC_IMAGE_MODE_NORMAL;
  src->min_ev_compensation = DEFAULT_MIN_EV_COMPENSATION;
  src->max_ev_compensation = DEFAULT_MAX_EV_COMPENSATION;
  src->ev_step = 0.0f;
  src->width = 0;
  src->height = 0;
  src->fps_n = 0;
  src->fps_d = 1;
  src->target_bitrate = DEFAULT_TARGET_BITRATE;
  src->jpeg_quality = DEFAULT_JPEG_QUALITY;

  gst_droidcamsrc_photography_init (src);

  src->vfsrc = gst_droidcamsrc_create_pad (src,
      GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME, FALSE);
  src->vfsrc->negotiate = gst_droidcamsrc_vfsrc_negotiate;

  src->imgsrc = gst_droidcamsrc_create_pad (src,
      GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME, TRUE);
  src->imgsrc->negotiate = gst_droidcamsrc_imgsrc_negotiate;

  src->vidsrc = gst_droidcamsrc_create_pad (src,
      GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME, TRUE);
  src->vidsrc->adjust_segment = TRUE;
  src->vidsrc->negotiate = gst_droidcamsrc_vidsrc_negotiate;

  /* create the modes after we create the pads because the modes need the pads */
  src->image = gst_droidcamsrc_mode_new_image (src);
  src->video = gst_droidcamsrc_mode_new_video (src);
  src->active_mode = NULL;

  src->post_preview = DEFAULT_POST_PREVIEW;
  src->preview_caps = NULL;
  src->preview_filter = NULL;
  src->preview_pipeline =
      gst_camerabin_create_preview_pipeline (GST_ELEMENT_CAST (src), NULL);

  GST_OBJECT_FLAG_SET (src, GST_ELEMENT_FLAG_SOURCE);
}

static void
gst_droidcamsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (object);
  GArray *supported_image_modes = NULL;
  int mode = DEFAULT_IMAGE_MODE;

  if (gst_droidcamsrc_photography_get_property (src, prop_id, value, pspec)) {
    return;
  }

  switch (prop_id) {
    case PROP_DEVICE_PARAMETERS:
      g_rec_mutex_lock (&src->dev_lock);
      g_value_set_pointer (value, src->dev
          && src->dev->params ? src->dev->params->params : NULL);
      g_rec_mutex_unlock (&src->dev_lock);
      break;

    case PROP_CAMERA_DEVICE:
      g_value_set_int (value, src->camera_device);
      break;

    case PROP_MODE:
      g_value_set_enum (value, src->mode);
      break;

    case PROP_READY_FOR_CAPTURE:
      g_mutex_lock (&src->capture_lock);
      g_value_set_boolean (value, src->captures == 0);
      g_mutex_unlock (&src->capture_lock);
      break;

    case PROP_MAX_ZOOM:
      g_value_set_float (value, src->max_zoom);
      break;

    case PROP_VIDEO_TORCH:
      g_value_set_boolean (value, src->video_torch);
      break;

    case PROP_MIN_EV_COMPENSATION:
      g_value_set_float (value, src->min_ev_compensation);
      break;

    case PROP_MAX_EV_COMPENSATION:
      g_value_set_float (value, src->max_ev_compensation);
      break;

    case PROP_FACE_DETECTION:
      g_value_set_boolean (value, src->face_detection);
      break;

    case PROP_IMAGE_NOISE_REDUCTION:
      g_value_set_boolean (value, src->image_noise_reduction);
      break;

    case PROP_SENSOR_DIRECTION:
      g_value_set_int (value, src->info[src->camera_device].direction);
      break;

    case PROP_SENSOR_MOUNT_ANGLE:
    case PROP_SENSOR_ORIENTATION:
      if (!gst_droidcamsrc_get_hw (src)) {
        g_value_set_int (value, 0);
      } else {
        g_value_set_int (value, src->info[src->camera_device].orientation * 90);
      }
      break;

    case PROP_IMAGE_MODE:
      g_value_set_flags (value, src->image_mode);
      break;

    case PROP_SUPPORTED_IMAGE_MODES:
      supported_image_modes = g_array_new (FALSE, FALSE, sizeof (gint));
      g_array_append_val (supported_image_modes, mode);

      if (gst_droidcamsrc_quirks_get_quirk (src->quirks, "zsl")) {
        mode = GST_DROIDCAMSRC_IMAGE_MODE_ZSL;
        g_array_append_val (supported_image_modes, mode);
      }

      if (gst_droidcamsrc_quirks_get_quirk (src->quirks, "hdr")) {
        mode = GST_DROIDCAMSRC_IMAGE_MODE_HDR;
        g_array_append_val (supported_image_modes, mode);
      }

      if (gst_droidcamsrc_is_zsl_and_hdr_supported (src)) {
        mode = GST_DROIDCAMSRC_IMAGE_MODE_ZSL;
        mode |= GST_DROIDCAMSRC_IMAGE_MODE_HDR;
        g_array_append_val (supported_image_modes, mode);
      }

      g_value_set_pointer (value, supported_image_modes);
      break;

    case PROP_TARGET_BITRATE:
      g_value_set_int (value, src->target_bitrate);
      break;

    case PROP_POST_PREVIEW:
      g_value_set_boolean (value, src->post_preview);
      break;

    case PROP_PREVIEW_CAPS:
      if (src->preview_caps)
        gst_value_set_caps (value, src->preview_caps);
      break;

    case PROP_PREVIEW_FILTER:
      if (src->preview_filter)
        g_value_set_object (value, src->preview_filter);
      break;

    case PROP_JPEG_QUALITY:
      g_value_set_uint (value, src->jpeg_quality);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_droidcamsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (object);

  if (gst_droidcamsrc_photography_set_property (src, prop_id, value, pspec)) {
    return;
  }

  switch (prop_id) {
    case PROP_CAMERA_DEVICE:
      src->camera_device = g_value_get_int (value);
      g_rec_mutex_lock (&src->dev_lock);
      if (src->dev && src->dev->info) {
        GST_WARNING_OBJECT (src,
            "changing camera-device to %d will take effect next time the camera is opened.",
            src->camera_device);
      } else {
        GST_INFO_OBJECT (src, "camera device set to %d", src->camera_device);
      }
      g_rec_mutex_unlock (&src->dev_lock);

      g_object_notify (G_OBJECT (src), "sensor-orientation");
      g_object_notify (G_OBJECT (src), "sensor-mount-angle");
      break;

    case PROP_MODE:
    {
      GstCameraBinMode mode = g_value_get_enum (value);

      GST_INFO_OBJECT (src, "setting capture mode to: %d", mode);

      if (src->mode == mode) {
        GST_INFO_OBJECT (src, "not resetting the same mode");
        break;
      }

      g_mutex_lock (&src->capture_lock);
      if (src->captures > 0) {
        GST_WARNING_OBJECT (src, "cannot set capture mode while capturing");
        g_mutex_unlock (&src->capture_lock);
        break;
      }

      g_mutex_unlock (&src->capture_lock);

      src->mode = mode;

      if (src->active_mode != NULL) {
        g_rec_mutex_lock (&src->dev_lock);

        /* deactivate old mode */
        gst_droidcamsrc_mode_deactivate (src->active_mode);
        src->active_mode = NULL;

        /* activate mode. */
        gst_droidcamsrc_select_and_activate_mode (src);

        /* set mode settings */
        gst_droidcamsrc_apply_mode_settings (src, SET_AND_APPLY);

        g_rec_mutex_unlock (&src->dev_lock);
      }
    }

      break;

    case PROP_VIDEO_TORCH:
      /* set value */
      src->video_torch = g_value_get_boolean (value);
      /* apply */
      gst_droidcamsrc_apply_mode_settings (src, SET_AND_APPLY);
      break;

    case PROP_FACE_DETECTION:
      src->face_detection = g_value_get_boolean (value);
      gst_droidcamsrc_apply_mode_settings (src, SET_AND_APPLY);
      break;

    case PROP_IMAGE_NOISE_REDUCTION:
      src->image_noise_reduction = g_value_get_boolean (value);
      gst_droidcamsrc_apply_mode_settings (src, SET_AND_APPLY);
      break;

    case PROP_IMAGE_MODE:
      src->image_mode = g_value_get_flags (value);
      gst_droidcamsrc_apply_mode_settings (src, SET_AND_APPLY);
      break;

    case PROP_TARGET_BITRATE:
      src->target_bitrate = g_value_get_int (value);
      break;

    case PROP_POST_PREVIEW:
      src->post_preview = g_value_get_boolean (value);

      if (src->dev) {
        gst_droidcamsrc_dev_update_preview_callback_flag (src->dev);
      }

      break;

    case PROP_PREVIEW_CAPS:{
      GstCaps *new_caps;

      new_caps = (GstCaps *) gst_value_get_caps (value);
      if (new_caps == NULL) {
        new_caps = gst_caps_new_any ();
      } else {
        new_caps = gst_caps_ref (new_caps);
      }

      if (!src->preview_caps || !gst_caps_is_equal (src->preview_caps, new_caps)) {
        gst_caps_replace (&src->preview_caps, new_caps);

        if (src->preview_pipeline) {
          GST_DEBUG_OBJECT (src,
              "Setting preview pipeline caps %" GST_PTR_FORMAT,
              src->preview_caps);
          gst_camerabin_preview_set_caps (src->preview_pipeline,
              src->preview_caps);
        }
      } else {
        GST_DEBUG_OBJECT (src, "New preview caps equal current preview caps");
      }
      gst_caps_unref (new_caps);
    }
      break;

    case PROP_PREVIEW_FILTER:
      if (src->preview_filter)
        gst_object_unref (src->preview_filter);
      src->preview_filter = g_value_dup_object (value);
      if (!gst_camerabin_preview_set_filter (src->preview_pipeline,
              src->preview_filter)) {
        GST_WARNING_OBJECT (src,
            "Cannot change preview filter, is element in NULL state?");
      }
      break;

    case PROP_JPEG_QUALITY:
      src->jpeg_quality = g_value_get_uint(value);
      gst_droidcamsrc_apply_mode_settings (src, SET_AND_APPLY);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_droidcamsrc_finalize (GObject * object)
{
  GstDroidCamSrc *src;

  src = GST_DROIDCAMSRC (object);

  GST_DEBUG_OBJECT (src, "finalize");

  gst_droidcamsrc_mode_free (src->image);
  gst_droidcamsrc_mode_free (src->video);

  gst_droidcamsrc_destroy_pad (src->vfsrc);
  gst_droidcamsrc_destroy_pad (src->imgsrc);
  gst_droidcamsrc_destroy_pad (src->vidsrc);

  g_mutex_clear (&src->capture_lock);

  gst_droidcamsrc_photography_destroy (src);

  gst_droidcamsrc_quirks_destroy (src->quirks);

  g_rec_mutex_clear (&src->dev_lock);

  if (src->preview_pipeline) {
    gst_camerabin_destroy_preview_pipeline (src->preview_pipeline);
    src->preview_pipeline = NULL;
  }

  if (src->preview_caps) {
    gst_caps_replace (&src->preview_caps, NULL);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_droidcamsrc_fill_info (GstDroidCamSrc * src, GstDroidCamSrcCamInfo * target,
    int camera_device)
{
  DroidMediaCameraInfo info;

  if (droid_media_camera_get_info (&info, camera_device) == false) {
    GST_WARNING_OBJECT (src, "Cannot get camera info for %d", camera_device);
    return FALSE;
  }

  target->num = camera_device;
  target->direction =
      info.facing ==
      DROID_MEDIA_CAMERA_FACING_FRONT ? NEMO_GST_META_DEVICE_DIRECTION_FRONT
      : NEMO_GST_META_DEVICE_DIRECTION_BACK;
  target->orientation = info.orientation / 90;

  GST_INFO_OBJECT (src, "camera %d is facing %d with orientation %d",
      target->num, target->direction, target->orientation);
  return TRUE;
}

static gboolean
gst_droidcamsrc_get_hw (GstDroidCamSrc * src)
{
  int x, num;

  GST_DEBUG_OBJECT (src, "get hw");

  if (src->info != NULL) {
    GST_DEBUG_OBJECT (src, "already have info");
    return TRUE;
  }

  num = droid_media_camera_get_number_of_cameras ();
  GST_INFO_OBJECT (src, "Found %d cameras", num);

  if (num < 0) {
    GST_ERROR_OBJECT (src, "no camera hardware found");
    return FALSE;
  }

  src->info = g_malloc0 (num * sizeof (GstDroidCamSrcCamInfo));

  for (x = 0; x < num; x++) {
    gboolean found;

    found = gst_droidcamsrc_fill_info (src, &src->info[x], x);
    if (!found) {
      GST_WARNING_OBJECT (src, "cannot find camera %d", x);
    }
  }

  return TRUE;
}

static GstDroidCamSrcCamInfo *
gst_droidcamsrc_find_camera_device (GstDroidCamSrc * src)
{
  int num = droid_media_camera_get_number_of_cameras ();

  if (src->camera_device < num) {
    return &src->info[src->camera_device];
  } else {
    GST_ERROR_OBJECT (src, "cannot find camera %d", src->camera_device);
  }

  return NULL;
}

static GstStateChangeReturn
gst_droidcamsrc_change_state (GstElement * element, GstStateChange transition)
{
  GstDroidCamSrc *src;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  src = GST_DROIDCAMSRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      GstDroidCamSrcCamInfo *info;
      const GstDroidCamSrcQuirk *quirk;
      gboolean quirk_is_property = FALSE;

      if (!gst_droidcamsrc_get_hw (src)) {
        ret = GST_STATE_CHANGE_FAILURE;
        break;
      }

      if (src->preview_pipeline == NULL) {
        /* failed to create preview pipeline, fail state change */
        ret = GST_STATE_CHANGE_FAILURE;
      }

      if (src->preview_caps) {
        GST_DEBUG_OBJECT (src,
            "Setting preview pipeline caps %" GST_PTR_FORMAT,
            src->preview_caps);
        gst_camerabin_preview_set_caps (src->preview_pipeline,
            src->preview_caps);
      }

      src->dev =
          gst_droidcamsrc_dev_new (src->vfsrc, src->imgsrc,
          src->vidsrc, &src->dev_lock);

      /* find the device */
      info = gst_droidcamsrc_find_camera_device (src);

      if (!info) {
        ret = GST_STATE_CHANGE_FAILURE;
        break;
      }

      quirk = gst_droidcamsrc_quirks_get_quirk (src->quirks, "start-up");
      if (quirk) {
        quirk_is_property = gst_droidcamsrc_quirk_is_property (quirk);
      }

      GST_DEBUG_OBJECT (src, "using camera device %i", info->num);

      if (!gst_droidcamsrc_dev_open (src->dev, info)) {
        ret = GST_STATE_CHANGE_FAILURE;
        break;
      }

      if (quirk && !quirk_is_property) {
        gst_droidcamsrc_quirks_apply_quirk (src->quirks, src,
            src->dev->info->direction, src->mode, quirk, TRUE);
      }

      if (!gst_droidcamsrc_dev_init (src->dev)) {
        ret = GST_STATE_CHANGE_FAILURE;
        break;
      }

      if (quirk && quirk_is_property) {
        gst_droidcamsrc_quirks_apply_quirk (src->quirks, src,
            src->dev->info->direction, src->mode, quirk, TRUE);
      }

      /* now that we have camera parameters, we can update min and max ev-compensation */
      gst_droidcamsrc_update_ev_compensation_bounds (src);

      /* and the photography parameters */
      gst_droidcamsrc_photography_update_params (src);

      /* And we can also detect the supported image modes. In reality the only thing
         we are unable to detect until this moment is _ZSL_AND_HDR */
      g_object_notify (G_OBJECT (src), "supported-image-modes");
    }

      break;

    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Now add the needed orientation tag */
      gst_droidcamsrc_add_vfsrc_orientation_tag (src);

      /* Update photos jpeg quality */
      gst_droidcamsrc_update_jpeg_quality(src);

      /* without this the preview pipeline will not post buffer
       * messages on the pipeline */
      gst_element_set_state (src->preview_pipeline->pipeline,
          GST_STATE_PLAYING);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* set initial photography parameters */
      gst_droidcamsrc_photography_apply (src, SET_ONLY);

      /* send flush stop if we start it in the previous transition */
      if (GST_PAD_IS_FLUSHING (src->vfsrc->pad)) {
        if (!gst_pad_push_event (src->vfsrc->pad,
                gst_event_new_flush_stop ( /* reset_time */ TRUE))) {
          ret = GST_STATE_CHANGE_FAILURE;
          break;
        }
      }

      /* activate mode */
      if (!gst_droidcamsrc_select_and_activate_mode (src)) {
        ret = GST_STATE_CHANGE_FAILURE;
        break;
      }

      /* apply mode settings */
      gst_droidcamsrc_apply_mode_settings (src, SET_ONLY);

      /* now start */
      if (!gst_droidcamsrc_dev_start (src->dev, FALSE)) {
        ret = GST_STATE_CHANGE_FAILURE;
      }

      src->captures = 0;
      g_object_notify (G_OBJECT (src), "ready-for-capture");

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
    /**
     * Send flush start to make sure the sink will drop its buffers.
     *
     * It is required to flush it before calling gst_droidcamsrc_dev_stop()
     * to avoid deadlock in android::BufferQueueProducer::dequeueBuffer()
     */
      g_mutex_lock (&src->vfsrc->lock);
      GST_INFO_OBJECT (src, "pushing flush start");
      gst_pad_push_event (src->vfsrc->pad, gst_event_new_flush_start ());
      g_mutex_unlock (&src->vfsrc->lock);

      /* TODO: stop recording if we are recording */
      gst_droidcamsrc_dev_stop (src->dev);
      src->captures = 0;

      /* deactivate mode */
      if (src->active_mode) {
        gst_droidcamsrc_mode_deactivate (src->active_mode);
        src->active_mode = NULL;
      }

      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_element_set_state (src->preview_pipeline->pipeline, GST_STATE_READY);
      break;

    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_droidcamsrc_dev_deinit (src->dev);
      gst_droidcamsrc_dev_close (src->dev);
      gst_droidcamsrc_dev_destroy (src->dev);
      src->dev = NULL;

      g_free (src->info);
      src->info = NULL;

      gst_element_set_state (src->preview_pipeline->pipeline, GST_STATE_NULL);
      break;

    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_SUCCESS
      && (transition == GST_STATE_CHANGE_READY_TO_PAUSED
          || transition == GST_STATE_CHANGE_PLAYING_TO_PAUSED)) {
    ret = GST_STATE_CHANGE_NO_PREROLL;
  }

  return ret;
}

static gboolean
gst_droidcamsrc_handle_roi_event (GstDroidCamSrc * src,
    const GstStructure * structure)
{
  /* the code below is mostly based on subdevsrc gst_subdevsrc_libomap3camd_handle_roi_event */
  guint width, height, count;
  guint w, h, top, left, prio;
  const GValue *regions;
  guint x;
  GPtrArray *focus_areas, *metering_areas;
  gboolean reset_focus_areas = FALSE, reset_metering_areas = FALSE;
  gint max_focus_areas;
  gint max_metering_areas;
  guint type;

#define SET_ROI_PARAM(a,n)						\
  do {									\
    gchar *param;							\
    /* we want a null terminated array */				\
    g_ptr_array_add (a, NULL);						\
    param = g_strjoinv (",", (gchar **)a->pdata);			\
    gst_droidcamsrc_params_set_string (src->dev->params, n, param);	\
    g_free (param);							\
  } while (0)

  GST_DEBUG_OBJECT (src, "roi event");

  if (!src->dev || !src->dev->params) {
    GST_WARNING_OBJECT (src, "camera not running");
    return FALSE;
  }

  max_focus_areas =
      gst_droidcamsrc_params_get_int (src->dev->params, "max-num-focus-areas");
  max_metering_areas =
      gst_droidcamsrc_params_get_int (src->dev->params,
      "max-num-metering-areas");

  if (max_focus_areas == 0 && max_metering_areas == 0) {
    GST_WARNING_OBJECT (src, "focus and metering areas are unsupported");
    return FALSE;
  }

  if (!gst_structure_get_uint (structure, "frame-width", &width)
      || !gst_structure_get_uint (structure, "frame-height", &height)) {
    GST_WARNING_OBJECT (src, "cannot read width or height from roi event");
    return FALSE;
  }

  if (!gst_structure_get_uint (structure, "type", &type)) {
    GST_DEBUG_OBJECT (src, "assuming focus and metering types for RoI event");
    type = GST_DROIDCAMSRC_ROI_FOCUS_AREA | GST_DROIDCAMSRC_ROI_METERING_AREA;
  }

  regions = gst_structure_get_value (structure, "regions");
  if (!regions) {
    GST_WARNING_OBJECT (src, "RoI event missing regions data");
    return FALSE;
  }

  count = gst_value_list_get_size (regions);

  max_focus_areas = MIN (count, max_focus_areas);
  max_metering_areas = MIN (count, max_metering_areas);

  /* an extra element for the terminating NULL */
  focus_areas = g_ptr_array_new_full (max_focus_areas + 1, g_free);
  metering_areas = g_ptr_array_new_full (max_metering_areas + 1, g_free);

  for (x = 0; x < count; x++) {
    const GstStructure *rs;
    const GValue *region = gst_value_list_get_value (regions, x);
    rs = gst_value_get_structure (region);
    if (!gst_structure_get_uint (rs, "region-x", &left)
        || !gst_structure_get_uint (rs, "region-y", &top)
        || !gst_structure_get_uint (rs, "region-w", &w)
        || !gst_structure_get_uint (rs, "region-h", &h)) {
      GST_WARNING_OBJECT (src, "incorrect RoI value %d", x);
      continue;
    }

    if (!gst_structure_get_uint (rs, "region-priority", &prio)) {
      prio = 1;
    }

    GST_DEBUG_OBJECT (src, "RoI: x=%d, y=%d, w=%d, h=%d, pri=%d",
        left, top, w, h, prio);

    if (prio == 0) {
      GST_DEBUG_OBJECT (src, "resetting RoI");

      if (type & GST_DROIDCAMSRC_ROI_FOCUS_AREA) {
        reset_focus_areas = TRUE;
      }

      if (type & GST_DROIDCAMSRC_ROI_METERING_AREA) {
        reset_metering_areas = TRUE;
      }

      /* no point in continuing */
      break;
    } else if (prio > 1000) {
      GST_WARNING_OBJECT (src, "adjusting priority for rectangle %d from %d", x,
          prio);
      prio = 1000;
    }

    /* now scale the rectangle */
    top = gst_util_uint64_scale (top, 2000, height) - 1000;
    left = gst_util_uint64_scale (left, 2000, width) - 1000;
    w = gst_util_uint64_scale (w, 2000, width);
    h = gst_util_uint64_scale (h, 2000, height);

    GST_DEBUG_OBJECT (src, "adjusted RoI: x=%d, y=%d, w=%d, h=%d, pri=%d",
        left, top, w, h, prio);

    if (type & GST_DROIDCAMSRC_ROI_FOCUS_AREA
        && focus_areas->len < max_focus_areas) {
      g_ptr_array_add (focus_areas, g_strdup_printf ("(%d,%d,%d,%d,%d)", left,
              top, left + w, top + h, prio));
    }

    if (type & GST_DROIDCAMSRC_ROI_METERING_AREA
        && metering_areas->len < max_metering_areas) {
      g_ptr_array_add (metering_areas, g_strdup_printf ("(%d,%d,%d,%d,%d)",
              left, top, left + w, top + h, prio));
    }
  }

  if (reset_focus_areas) {
    gst_droidcamsrc_params_set_string (src->dev->params, "focus-areas",
        "(0,0,0,0,0)");
  } else {
    SET_ROI_PARAM (focus_areas, "focus-areas");
  }

  if (reset_metering_areas) {
    gst_droidcamsrc_params_set_string (src->dev->params, "metering-areas",
        "(0,0,0,0,0)");
  } else {
    SET_ROI_PARAM (metering_areas, "metering-areas");
  }

  g_ptr_array_free (focus_areas, TRUE);
  g_ptr_array_free (metering_areas, TRUE);

  if (!gst_droidcamsrc_apply_params (src)) {
    GST_WARNING_OBJECT (src, "failed to apply parameters");
  }

  return TRUE;
}

static gboolean
gst_droidcamsrc_send_event (GstElement * element, GstEvent * event)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (element);
  gboolean res = FALSE;

  GST_DEBUG_OBJECT (src, "handling event %p %" GST_PTR_FORMAT, event, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_CAPS:
    case GST_EVENT_STREAM_START:
    case GST_EVENT_BUFFERSIZE:
    case GST_EVENT_SEGMENT_DONE:
    case GST_EVENT_GAP:
    case GST_EVENT_SEEK:
    case GST_EVENT_TOC_SELECT:
    case GST_EVENT_RECONFIGURE:
    case GST_EVENT_LATENCY:
    case GST_EVENT_STEP:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_SEGMENT:
    case GST_EVENT_TOC:
    case GST_EVENT_SINK_MESSAGE:
    case GST_EVENT_QOS:
    case GST_EVENT_UNKNOWN:
    case GST_EVENT_STREAM_COLLECTION:
    case GST_EVENT_STREAM_GROUP_DONE:
    case GST_EVENT_PROTECTION:
    case GST_EVENT_SELECT_STREAMS:
#if GST_CHECK_VERSION(1,18,0)
    case GST_EVENT_INSTANT_RATE_CHANGE:
    case GST_EVENT_INSTANT_RATE_SYNC_TIME:
#endif
      break;

    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      const gchar *name;
      const GstStructure *structure = gst_event_get_structure (event);
      if (!structure) {
        break;
      }

      name = gst_structure_get_name (structure);
      if (g_strcmp0 (name, "regions-of-interest")) {
        break;
      }

      /* now handle ROI event */
      res = gst_droidcamsrc_handle_roi_event (src, structure);
    }

      break;

      /* serialized events */
    case GST_EVENT_EOS:
    case GST_EVENT_TAG:
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_STICKY:
    case GST_EVENT_CUSTOM_BOTH:
      GST_OBJECT_LOCK (src);
      src->vfsrc->pending_events =
          g_list_append (src->vfsrc->pending_events, event);
      GST_OBJECT_UNLOCK (src);
      event = NULL;
      res = TRUE;
      break;

      /* non serialized events */
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    case GST_EVENT_CUSTOM_BOTH_OOB:
      res = gst_pad_push_event (src->vfsrc->pad, event);
      event = NULL;
      break;
    default:
      break;
  }

  if (event) {
    gst_event_unref (event);
  }

  return res;
}

static void
gst_droidcamsrc_class_init (GstDroidCamSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstCaps *caps;
  GstPadTemplate *tpl;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Camera source", "Source/Video/Device",
      "Android HAL camera source", "Mohammed Sameer <msameer@foolab.org>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&vf_src_template_factory));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&img_src_template_factory));

  /* encoded caps */
  caps = gst_droid_codec_get_all_caps (GST_DROID_CODEC_ENCODER_VIDEO);

  /* add raw caps */
  caps =
      gst_caps_merge (caps,
      gst_caps_from_string (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
          (GST_CAPS_FEATURE_MEMORY_DROID_VIDEO_META_DATA, "{YV12}")));
  tpl =
      gst_pad_template_new (GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME, GST_PAD_SRC,
      GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (gstelement_class, tpl);

  gobject_class->set_property = gst_droidcamsrc_set_property;
  gobject_class->get_property = gst_droidcamsrc_get_property;
  gobject_class->finalize = gst_droidcamsrc_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_droidcamsrc_change_state);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_droidcamsrc_send_event);

  /* Add camera-device property only if cameras have been found */
  if (droid_media_camera_get_number_of_cameras () > 0) {
    g_object_class_install_property (gobject_class, PROP_CAMERA_DEVICE,
        g_param_spec_int ("camera-device", "Camera device",
            "Defines which camera device should be used",
            0,
            droid_media_camera_get_number_of_cameras () - 1,
            DEFAULT_CAMERA_DEVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  }

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "Capture mode (image or video)",
          GST_TYPE_CAMERABIN_MODE,
          DEFAULT_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_READY_FOR_CAPTURE,
      g_param_spec_boolean ("ready-for-capture", "Ready for capture",
          "Element is ready for another capture",
          TRUE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_ZOOM,
      g_param_spec_float ("max-zoom",
          "Maximum zoom level (note: may change "
          "depending on resolution/implementation)",
          "Android zoom factor", 1.0f, G_MAXFLOAT,
          DEFAULT_MAX_ZOOM, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MIN_EV_COMPENSATION,
      g_param_spec_float ("min-ev-compensation",
          "Minimum exposure compensation",
          "Minimum exposure compensation", -2.5f, G_MAXFLOAT,
          DEFAULT_MAX_EV_COMPENSATION,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_EV_COMPENSATION,
      g_param_spec_float ("max-ev-compensation",
          "Maximum exposure compensation",
          "Maximum exposure compensation", 2.5f, G_MAXFLOAT,
          DEFAULT_MAX_EV_COMPENSATION,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VIDEO_TORCH,
      g_param_spec_boolean ("video-torch", "Video torch",
          "Sets torch light on or off for video recording", DEFAULT_VIDEO_TORCH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FACE_DETECTION,
      g_param_spec_boolean ("face-detection", "Face Detection",
          "Face detection", DEFAULT_FACE_DETECTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_IMAGE_NOISE_REDUCTION,
      g_param_spec_boolean ("image-noise-reduction", "Image noise reduction",
          "Vendor specific image noise reduction",
          DEFAULT_IMAGE_NOISE_REDUCTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SENSOR_DIRECTION,
      g_param_spec_int ("sensor-direction", "Sensor direction",
          "Sensor direction as reported by HAL (0=back, 1=front)", 0, 1,
          DEFAULT_SENSOR_DIRECTION, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SENSOR_ORIENTATION,
      g_param_spec_int ("sensor-orientation", "Sensor orientation",
          "Sensor orientation as reported by HAL", 0, 270,
          DEFAULT_SENSOR_ORIENTATION,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SENSOR_MOUNT_ANGLE,
      g_param_spec_int ("sensor-mount-angle", "Sensor mount angle",
          "Sensor orientation as reported by HAL (deprecated, use sensor-orientation)",
          0, 270, DEFAULT_SENSOR_ORIENTATION,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_DEPRECATED));

  g_object_class_install_property (gobject_class, PROP_DEVICE_PARAMETERS,
      g_param_spec_pointer ("device-parameters", "Device parameters",
          "The GHash table of the GstDroidCamSrcParams struct currently used",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_IMAGE_MODE,
      g_param_spec_flags ("image-mode", "Image mode",
          "Image mode (normal, zsl, hdr)",
          GST_TYPE_DROIDCAMSRC_IMAGE_MODE,
          DEFAULT_IMAGE_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SUPPORTED_IMAGE_MODES,
      g_param_spec_pointer ("supported-image-modes", "Supported image modes",
          "Image modes supported by HAL",
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TARGET_BITRATE,
      g_param_spec_int ("target-bitrate", "Target Bitrate",
          "Target bitrate", 0, G_MAXINT,
          DEFAULT_TARGET_BITRATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_SUPPORTED_WB_MODES,
      g_param_spec_variant ("supported-wb-modes",
          "Supported white balance modes", "Supported white balance modes",
          G_VARIANT_TYPE_VARIANT, NULL, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_SUPPORTED_COLOR_TONES,
      g_param_spec_variant ("supported-color-tones", "Supported color tones",
          "Supported color tones", G_VARIANT_TYPE_VARIANT, NULL,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_SUPPORTED_SCENE_MODES,
      g_param_spec_variant ("supported-scene-modes",
          "Supported scene modes", "Supported scene modes",
          G_VARIANT_TYPE_VARIANT, NULL, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_SUPPORTED_FLASH_MODES,
      g_param_spec_variant ("supported-flash-modes", "Supported flash modes",
          "Supported flash modes", G_VARIANT_TYPE_VARIANT, NULL,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_SUPPORTED_FOCUS_MODES,
      g_param_spec_variant ("supported-focus-modes", "Supported focus modes",
          "Supported focus modes", G_VARIANT_TYPE_VARIANT, NULL,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_SUPPORTED_ISO_SPEEDS,
      g_param_spec_variant ("supported-iso-speeds", "Supported ISO speeds",
          "Supported ISO speeds", G_VARIANT_TYPE_VARIANT, NULL,
          G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_JPEG_QUALITY,
      g_param_spec_int ("jpeg-quality", "JPEG quality",
          "Android HAL's jpeg quality", DEFAULT_MIN_JPEG_QUALITY,
          DEFAULT_MAX_JPEG_QUALITY, DEFAULT_JPEG_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* camerabin interface */
  g_object_class_install_property (gobject_class, PROP_POST_PREVIEW,
      g_param_spec_boolean ("post-previews", "Post Previews",
          "If capture preview images should be posted to the bus",
          DEFAULT_POST_PREVIEW, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PREVIEW_CAPS,
      g_param_spec_boxed ("preview-caps", "Preview caps",
          "The caps of the preview image to be posted (NULL means ANY)",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PREVIEW_FILTER,
      g_param_spec_object ("preview-filter", "Preview filter",
          "A custom preview filter to process preview image data",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_droidcamsrc_photography_add_overrides (gobject_class);

  /* Signals */
  droidcamsrc_signals[START_CAPTURE_SIGNAL] =
      g_signal_new_class_handler ("start-capture",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_droidcamsrc_start_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  droidcamsrc_signals[STOP_CAPTURE_SIGNAL] =
      g_signal_new_class_handler ("stop-capture",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_CALLBACK (gst_droidcamsrc_stop_capture),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void
gst_droidcamsrc_loop (gpointer user_data)
{
  GstFlowReturn ret;
  GstDroidCamSrcPad *data = (GstDroidCamSrcPad *) user_data;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (data->pad));
  GstBuffer *buffer = NULL;
  GstPad *pad = data->pad;

  GList *events;

  GST_LOG_OBJECT (pad, "loop");

  g_mutex_lock (&data->lock);

  if (!data->running) {
    GST_DEBUG_OBJECT (pad, "task is not running");
    g_mutex_unlock (&data->lock);
    goto exit;
  }

  g_mutex_unlock (&data->lock);

  /* stream start */
  if (G_UNLIKELY (data->open_stream)) {
    gchar *stream_id;
    GstEvent *event;

    stream_id =
        gst_pad_create_stream_id (data->pad, GST_ELEMENT_CAST (src),
        GST_PAD_NAME (data->pad));

    GST_DEBUG_OBJECT (pad, "Pushing STREAM_START for pad");
    event = gst_event_new_stream_start (stream_id);
    gst_event_set_group_id (event, gst_util_group_id_next ());
    if (!gst_pad_push_event (data->pad, event)) {
      GST_ERROR_OBJECT (src, "failed to push STREAM_START event for pad %s",
          GST_PAD_NAME (data->pad));
    }

    g_free (stream_id);
    data->open_stream = FALSE;
  }

  g_mutex_lock (&data->lock);

  if (!data->running) {
    GST_DEBUG_OBJECT (pad, "task is not running");
    g_mutex_unlock (&data->lock);
    goto exit;
  }

  buffer = g_queue_pop_head (data->queue);
  if (buffer) {
    g_mutex_unlock (&data->lock);
    goto out;
  }

  if (!buffer) {
    g_cond_wait (&data->cond, &data->lock);
    buffer = g_queue_pop_head (data->queue);
  }

  g_mutex_unlock (&data->lock);

  if (!buffer) {
    /* we got signaled to exit */
    goto exit;
  } else {
    goto out;
  }

  return;

exit:
  return;

out:
  /* segment */
  if (G_UNLIKELY (data->open_segment)) {
    GstEvent *event;

    GST_DEBUG_OBJECT (pad, "Pushing SEGMENT");

    if (data->adjust_segment) {
      data->segment.start = GST_BUFFER_PTS (buffer);
    }

    event = gst_event_new_segment (&data->segment);

    if (!gst_pad_push_event (data->pad, event)) {
      GST_ERROR_OBJECT (src, "failed to push SEGMENT event");
    }

    data->open_segment = FALSE;
  }

  /* pending events */
  GST_OBJECT_LOCK (src);
  /* queue lock is needed for vfsrc and imgsrc pads */
  g_mutex_lock (&data->lock);
  events = data->pending_events;
  data->pending_events = NULL;
  g_mutex_unlock (&data->lock);
  GST_OBJECT_UNLOCK (src);

  if (G_UNLIKELY (events)) {
    GList *tmp;
    for (tmp = events; tmp; tmp = g_list_next (tmp)) {
      GstEvent *ev = (GstEvent *) tmp->data;
      GST_LOG_OBJECT (pad, "pushing pending event %" GST_PTR_FORMAT, ev);
      gst_pad_push_event (data->pad, ev);
    }
    g_list_free (events);
  }

  /* finally we can push our buffer */
  GST_LOG_OBJECT (pad, "pushing buffer %p", buffer);
  ret = gst_pad_push (data->pad, buffer);

  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_INFO_OBJECT (src, "error %s pushing buffer through pad %s",
        gst_flow_get_name (ret), GST_PAD_NAME (data->pad));

    if (ret == GST_FLOW_EOS) {
      gst_pad_push_event (data->pad, gst_event_new_eos ());
    } else if (ret < GST_FLOW_OK && ret != GST_FLOW_FLUSHING) {
      GST_ELEMENT_ERROR (src, STREAM, FAILED,
          ("Internal data stream error."), ("stream stopped, reason %s",
              gst_flow_get_name (ret)));
    }
  }

  g_mutex_lock (&data->lock);
  data->pushed_buffers++;
  g_mutex_unlock (&data->lock);
}

static gboolean
gst_droidcamsrc_pad_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (parent);
  GstDroidCamSrcPad *data = gst_pad_get_element_private (pad);

  GST_INFO_OBJECT (src, "activating pad %s %d", GST_PAD_NAME (pad), active);

  if (mode != GST_PAD_MODE_PUSH) {
    GST_ERROR_OBJECT (src, "can activate pads in push mode only");
    return FALSE;
  }

  if (!data) {
    GST_ERROR_OBJECT (src, "cannot get pad private data");
    return FALSE;
  }

  if (active) {
    /* No need for locking here since the task is not running */
    data->running = TRUE;
    data->open_stream = TRUE;
    data->open_segment = TRUE;
    data->pushed_buffers = 0;
    if (!gst_pad_start_task (pad, gst_droidcamsrc_loop, data, NULL)) {
      GST_ERROR_OBJECT (src, "failed to start pad task");
      return FALSE;
    }
  } else {
    gboolean ret = FALSE;

    g_mutex_lock (&data->lock);
    data->running = FALSE;
    g_cond_signal (&data->cond);
    g_mutex_unlock (&data->lock);

    if (!gst_pad_stop_task (pad)) {
      GST_ERROR_OBJECT (src, "failed to stop pad task");
      ret = FALSE;
    } else {
      ret = TRUE;
    }

    g_mutex_lock (&data->lock);
    /* toss the queue */
    g_queue_foreach (data->queue, (GFunc) gst_buffer_unref, NULL);
    g_queue_clear (data->queue);
    g_mutex_unlock (&data->lock);

    GST_OBJECT_LOCK (src);

    /*
     * We have 2 pads making use of such lists:
     * - vfsrc which always accesses the list with the object lock so it should be safe.
     *   It's also safe for it because it's not running so the object lock is needed in
     *   order to prevent us from messing up the list if we happen to get events.
     * - imgsrc which is also not running. GstDRoidCamSrcDev is also gone long ago
     */
    if (data->pending_events) {
      g_list_free_full (data->pending_events, (GDestroyNotify) gst_event_unref);
      data->pending_events = NULL;
    }

    GST_OBJECT_UNLOCK (src);
    return ret;
  }

  return TRUE;
}

static gboolean
gst_droidcamsrc_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (parent);
  gboolean ret = FALSE;
  GstDroidCamSrcPad *data = gst_pad_get_element_private (pad);

  GST_LOG_OBJECT (src, "pad %s %" GST_PTR_FORMAT, GST_PAD_NAME (pad), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    case GST_EVENT_STREAM_START:
    case GST_EVENT_TAG:
    case GST_EVENT_BUFFERSIZE:
    case GST_EVENT_SINK_MESSAGE:
    case GST_EVENT_SEGMENT:
    case GST_EVENT_EOS:
    case GST_EVENT_TOC:
    case GST_EVENT_SEGMENT_DONE:
    case GST_EVENT_GAP:
    case GST_EVENT_QOS:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_STEP:
    case GST_EVENT_TOC_SELECT:
    case GST_EVENT_CUSTOM_UPSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    case GST_EVENT_CUSTOM_DOWNSTREAM_STICKY:
    case GST_EVENT_CUSTOM_BOTH:
    case GST_EVENT_CUSTOM_BOTH_OOB:
    case GST_EVENT_UNKNOWN:
    case GST_EVENT_STREAM_COLLECTION:
    case GST_EVENT_STREAM_GROUP_DONE:
    case GST_EVENT_PROTECTION:
    case GST_EVENT_SELECT_STREAMS:
#if GST_CHECK_VERSION(1,18,0)
    case GST_EVENT_INSTANT_RATE_CHANGE:
    case GST_EVENT_INSTANT_RATE_SYNC_TIME:
#endif
      ret = FALSE;
      break;

    case GST_EVENT_CAPS:
    case GST_EVENT_LATENCY:
      ret = TRUE;
      /* TODO: */
      break;

    case GST_EVENT_RECONFIGURE:
      g_mutex_lock (&src->capture_lock);
      if (data->capture_pad && src->captures > 0) {
        GST_ERROR_OBJECT (src, "cannot reconfigure pad %s while capturing",
            GST_PAD_NAME (data->pad));
        ret = FALSE;
      } else if (src->active_mode
          && gst_droidcamsrc_mode_pad_is_significant (src->active_mode, pad)) {
        ret = gst_droidcamsrc_mode_negotiate (src->active_mode, pad);
      } else {
        /* pad will negotiate later */
        ret = TRUE;
      }

      g_mutex_unlock (&src->capture_lock);

      break;

    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
      ret = TRUE;
      /* TODO: what do we do here? */
      break;
  }

  if (ret) {
    GST_LOG_OBJECT (src, "replying to %" GST_PTR_FORMAT, event);
  } else {
    GST_LOG_OBJECT (src, "discarding %" GST_PTR_FORMAT, event);
  }

  gst_event_unref (event);

  return ret;
}

static gboolean
gst_droidcamsrc_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (parent);
  gboolean ret = FALSE;
  GstDroidCamSrcPad *data = gst_pad_get_element_private (pad);
  GstCaps *caps = NULL;
  GstCaps *filter = NULL;
  GstCaps *query_caps = NULL;

  GST_DEBUG_OBJECT (src, "pad %s %" GST_PTR_FORMAT, GST_PAD_NAME (pad), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    case GST_QUERY_DURATION:
    case GST_QUERY_SEEKING:
    case GST_QUERY_JITTER:
    case GST_QUERY_RATE:
    case GST_QUERY_CONVERT:
    case GST_QUERY_BUFFERING:
    case GST_QUERY_URI:
    case GST_QUERY_DRAIN:
    case GST_QUERY_CONTEXT:
    case GST_QUERY_UNKNOWN:
    case GST_QUERY_ALLOCATION:
    case GST_QUERY_SEGMENT:
#if GST_CHECK_VERSION(1,16,0)
    case GST_QUERY_BITRATE:
#endif
#if GST_CHECK_VERSION(1,22,0)
    case GST_QUERY_SELECTABLE:
#endif
      ret = FALSE;
      break;

    case GST_QUERY_ACCEPT_CAPS:
    {
      caps = gst_pad_get_pad_template_caps (data->pad);
      gst_query_parse_accept_caps (query, &query_caps);

      if (caps && query_caps && gst_caps_is_equal (caps, query_caps)) {
        gst_query_set_accept_caps_result (query, TRUE);
      } else {
        gst_query_set_accept_caps_result (query, FALSE);
      }

      if (caps) {
        gst_caps_unref (caps);
        caps = NULL;
      }

      ret = TRUE;
      break;
    }

    case GST_QUERY_SCHEDULING:
      gst_query_add_scheduling_mode (query, GST_PAD_MODE_PUSH);
      ret = TRUE;
      break;

    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 1, GST_FORMAT_TIME);
      ret = TRUE;
      break;

    case GST_QUERY_LATENCY:
      /* TODO: this assummes 7 buffers @ 30 FPS. Query from either bufferpool or camera params */
      gst_query_set_latency (query, TRUE, 33, 33 * 7);
      ret = TRUE;
      break;


    case GST_QUERY_CAPS:
      /* if we have a device already, return the caps supported by HAL otherwise
       * just return the pad template */
      g_rec_mutex_lock (&src->dev_lock);
      if (src->dev && src->dev->params) {
        if (data == src->vfsrc) {
          caps =
              gst_droidcamsrc_params_get_viewfinder_caps (src->dev->params,
              src->dev->viewfinder_format);
        } else if (data == src->imgsrc) {
          caps = gst_droidcamsrc_params_get_image_caps (src->dev->params);
        } else if (data == src->vidsrc) {
          caps = gst_droidcamsrc_get_video_caps_locked (src);
        }
      } else {
        caps = gst_pad_get_pad_template_caps (data->pad);
      }
      g_rec_mutex_unlock (&src->dev_lock);

      gst_query_parse_caps (query, &filter);
      if (caps && filter) {
        GstCaps *intersection =
            gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (caps);
        caps = intersection;
      }

      if (caps) {
        gst_query_set_caps_result (query, caps);
        ret = TRUE;
      } else {
        ret = FALSE;
      }

      if (caps) {
        gst_caps_unref (caps);
      }

      break;

    case GST_QUERY_CUSTOM:
    {
      const GstStructure *structure = gst_query_get_structure (query);
      if (structure
          && !g_strcmp0 (gst_structure_get_name (structure),
              GST_DROID_VIDEO_COLOR_FORMAT_QUERY_NAME)) {
        gint format = droid_media_camera_get_video_color_format (src->dev->cam);
        if (format == -1) {
          GST_WARNING_OBJECT (src, "failed to get video color format");
          ret = FALSE;
        } else {
          gst_droid_query_set_video_color_format (query, format);
          ret = TRUE;
        }
      } else {
        ret = FALSE;
      }
    }
      break;
  }

  if (ret) {
    GST_LOG_OBJECT (src, "replying to %" GST_PTR_FORMAT, query);
  } else {
    GST_LOG_OBJECT (src, "discarding %" GST_PTR_FORMAT, query);
  }

  return ret;
}

static gboolean
gst_droidcamsrc_vfsrc_negotiate (GstDroidCamSrcPad * data)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (data->pad));
  gboolean ret = FALSE;
  GstCaps *peer = NULL;
  GstCaps *our_caps = NULL;
  GstCapsFeatures *features;
  gchar *preview;
  GstVideoInfo info;
  gboolean use_raw_data = TRUE;
  GstBufferPool *pool = NULL;

  g_rec_mutex_lock (&src->dev_lock);

  GST_DEBUG_OBJECT (src, "vfsrc negotiate");

  our_caps =
      gst_droidcamsrc_params_get_viewfinder_caps (src->dev->params,
      src->dev->viewfinder_format);
  GST_DEBUG_OBJECT (src, "our caps %" GST_PTR_FORMAT, our_caps);

  if (!our_caps || gst_caps_is_empty (our_caps)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("failed to get caps from HAL"),
        (NULL));
    goto out;
  }

  peer = gst_pad_peer_query_caps (data->pad, our_caps);
  GST_DEBUG_OBJECT (src, "peer caps %" GST_PTR_FORMAT, peer);

  if (!peer || gst_caps_is_empty (peer)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("failed to negotiate caps"),
        (NULL));
    goto out;
  }

  gst_caps_unref (our_caps);
  our_caps = peer;
  peer = NULL;

  our_caps = gst_caps_make_writable (our_caps);
  our_caps = gst_droidcamsrc_pick_largest_resolution (src, our_caps);

  if (src->mode == MODE_IMAGE) {
    gst_droidcamsrc_params_choose_image_framerate (src->dev->params, our_caps);
  } else {
    gst_droidcamsrc_params_choose_video_framerate (src->dev->params, our_caps);
  }

  if (!gst_pad_push_event (data->pad, gst_event_new_caps (our_caps))) {
    GST_ERROR_OBJECT (src, "failed to set caps");
    goto out;
  }

  GST_DEBUG_OBJECT (src, "pad %s using caps %" GST_PTR_FORMAT,
      GST_PAD_NAME (data->pad), our_caps);

  if (!gst_video_info_from_caps (&info, our_caps)) {
    GST_ERROR_OBJECT (src, "failed to parse caps");
    goto out;
  }

  GST_OBJECT_LOCK (src);
  src->width = info.width;
  src->height = info.height;
  src->fps_n = info.fps_n;
  src->fps_d = info.fps_d;
  src->crop_rect.left = 0;
  src->crop_rect.top = 0;
  src->crop_rect.right = info.width;
  src->crop_rect.bottom = info.height;
  GST_OBJECT_UNLOCK (src);

  preview = g_strdup_printf ("%ix%i", info.width, info.height);
  gst_droidcamsrc_params_set_string (src->dev->params, "preview-size", preview);
  g_free (preview);

  features = gst_caps_get_features (our_caps, 0);

  use_raw_data =
      !gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_QUEUE_BUFFER);

  if (!use_raw_data) {
    /* Negotiate a buffer pool or allocate one now. */
    gint i;
    guint min, max;
    guint size;
    gint count;
    GstQuery *query = gst_query_new_allocation (our_caps, TRUE);

    if (!gst_pad_peer_query (data->pad, query)) {
      GST_DEBUG_OBJECT (src, "didn't get downstream ALLOCATION hints");
    }

    count = gst_query_get_n_allocation_pools (query);

    for (i = 0; i < count; ++i) {
      GstAllocator *allocator;
      GstStructure *config;

      gst_query_parse_nth_allocation_pool (query, i, &pool, &size, &min, &max);
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_get_allocator (config, &allocator, NULL);
      if (allocator
          && g_strcmp0 (allocator->mem_type,
              GST_ALLOCATOR_DROID_MEDIA_BUFFER) == 0) {
        break;
      } else {
        gst_object_unref (pool);
        pool = NULL;
      }
    }

    /* The downstream may have other ideas about what the pool size should be but the
     * queue we're working with has a fixed size so that's the number of buffers we'll
     * go with. */
    min = 0;
    max = droid_media_buffer_queue_length ();

    if (!pool) {
      /* A downstream which understands the queue buffers should also have provided a pool
       * but for completeness add this a fallback. */
      GstStructure *config;
      GstVideoInfo video_info;
      gst_video_info_from_caps (&video_info, our_caps);

      pool = gst_droid_buffer_pool_new ();
      size = video_info.finfo->format == GST_VIDEO_FORMAT_ENCODED
          ? 1 : video_info.size;

      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_set_params (config, our_caps, size, min, max);

      if (!gst_buffer_pool_set_config (pool, config)) {
        GST_ERROR_OBJECT (src, "Failed to set buffer pool configuration");
        gst_object_unref (pool);
        pool = NULL;
        ret = FALSE;
        goto out;
      }
    }
  }

  g_rec_mutex_lock (&src->dev_lock);
  src->dev->use_raw_data = use_raw_data;

  if (src->dev->pool) {
    gst_object_unref (src->dev->pool);
  }
  src->dev->pool = pool;

  g_rec_mutex_unlock (&src->dev_lock);

  ret = TRUE;

out:
  if (peer) {
    gst_caps_unref (peer);
  }

  if (our_caps) {
    gst_caps_unref (our_caps);
  }

  g_rec_mutex_unlock (&src->dev_lock);

  return ret;
}

static gboolean
gst_droidcamsrc_imgsrc_negotiate (GstDroidCamSrcPad * data)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (data->pad));
  gboolean ret = FALSE;
  GstCaps *peer = NULL;
  GstCaps *our_caps = NULL;
  gchar *pic;
  GstVideoInfo info;

  g_rec_mutex_lock (&src->dev_lock);

  GST_DEBUG_OBJECT (src, "imgsrc negotiate");

  our_caps = gst_droidcamsrc_params_get_image_caps (src->dev->params);
  GST_DEBUG_OBJECT (src, "our caps %" GST_PTR_FORMAT, our_caps);

  if (!our_caps || gst_caps_is_empty (our_caps)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("failed to get caps from HAL"),
        (NULL));
    goto out;
  }

  peer = gst_pad_peer_query_caps (data->pad, our_caps);
  GST_DEBUG_OBJECT (src, "peer caps %" GST_PTR_FORMAT, peer);

  if (!peer || gst_caps_is_empty (peer)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("failed to negotiate caps"),
        (NULL));
    goto out;
  }

  gst_caps_unref (our_caps);
  our_caps = peer;
  peer = NULL;

  our_caps = gst_caps_make_writable (our_caps);
  our_caps = gst_droidcamsrc_pick_largest_resolution (src, our_caps);
  gst_droidcamsrc_params_choose_image_framerate (src->dev->params, our_caps);

  if (!gst_pad_push_event (data->pad, gst_event_new_caps (our_caps))) {
    GST_ERROR_OBJECT (src, "failed to set caps");
    goto out;
  }

  GST_DEBUG_OBJECT (src, "pad %s using caps %" GST_PTR_FORMAT,
      GST_PAD_NAME (data->pad), our_caps);

  if (!gst_video_info_from_caps (&info, our_caps)) {
    GST_ERROR_OBJECT (src, "failed to parse caps");
    goto out;
  }

  pic = g_strdup_printf ("%ix%i", info.width, info.height);
  gst_droidcamsrc_params_set_string (src->dev->params, "picture-size", pic);
  g_free (pic);

  ret = TRUE;

out:
  if (peer) {
    gst_caps_unref (peer);
  }

  if (our_caps) {
    gst_caps_unref (our_caps);
  }

  g_rec_mutex_unlock (&src->dev_lock);

  return ret;
}

static gboolean
gst_droidcamsrc_vidsrc_negotiate (GstDroidCamSrcPad * data)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (data->pad));
  gboolean ret = FALSE;
  GstCaps *peer = NULL;
  GstCaps *our_caps = NULL;
  gchar *vid, *pic;
  GstVideoInfo info;

  g_rec_mutex_lock (&src->dev_lock);

  GST_DEBUG_OBJECT (src, "vidsrc negotiate");

  our_caps = gst_droidcamsrc_get_video_caps_locked (src);
  GST_DEBUG_OBJECT (src, "our caps %" GST_PTR_FORMAT, our_caps);

  if (!our_caps || gst_caps_is_empty (our_caps)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("failed to get caps from HAL"),
        (NULL));
    goto out;
  }

  peer = gst_pad_peer_query_caps (data->pad, our_caps);
  GST_DEBUG_OBJECT (src, "peer caps %" GST_PTR_FORMAT, peer);

  if (!peer || gst_caps_is_empty (peer)) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("failed to negotiate caps"),
        (NULL));
    goto out;
  }

  gst_caps_unref (our_caps);
  our_caps = peer;
  peer = NULL;

  our_caps = gst_caps_make_writable (our_caps);
  our_caps = gst_droidcamsrc_pick_largest_resolution (src, our_caps);

  gst_droidcamsrc_params_choose_framerate (src->dev->params, our_caps, NULL);

  if (!gst_pad_push_event (data->pad, gst_event_new_caps (our_caps))) {
    GST_ERROR_OBJECT (src, "failed to set caps");
    goto out;
  }

  GST_DEBUG_OBJECT (src, "pad %s using caps %" GST_PTR_FORMAT,
      GST_PAD_NAME (data->pad), our_caps);

  if (!gst_video_info_from_caps (&info, our_caps)) {
    GST_ERROR_OBJECT (src, "failed to parse caps");
    goto out;
  }

  vid = g_strdup_printf ("%ix%i", info.width, info.height);
  gchar *key =
      src->dev->params->
      has_separate_video_size_values ? "video-size" : "preview-size";
  gst_droidcamsrc_params_set_string (src->dev->params, key, vid);

  /* Now we need to find a picture size that is equal to our video size.
   * Some devices need to have a picture size otherwise the video mode viewfinder
   * behaves in a funny way and can be stretched or squeezed */
  pic = gst_droidcamsrc_find_picture_resolution (src, vid);
  g_free (vid);

  if (pic) {
    gst_droidcamsrc_params_set_string (src->dev->params, "picture-size", pic);
    g_free (pic);
  }

  ret = TRUE;

  if (info.finfo->format == GST_VIDEO_FORMAT_ENCODED) {
    GST_INFO_OBJECT (src, "using external recorder");
    src->dev->use_recorder = TRUE;
  } else {
    GST_INFO_OBJECT (src, "using raw recorder");
    src->dev->use_recorder = FALSE;
  }

  gst_droidcamsrc_recorder_update_vid (src->dev->recorder, &info, our_caps);

out:
  if (peer) {
    gst_caps_unref (peer);
  }

  if (our_caps) {
    gst_caps_unref (our_caps);
  }

  g_rec_mutex_unlock (&src->dev_lock);

  return ret;
}

gboolean
gst_droidcamsrc_apply_params (GstDroidCamSrc * src)
{
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (src, "apply params");

  ret = gst_droidcamsrc_dev_set_params (src->dev);

  if (!ret) {
    GST_ERROR_OBJECT (src, "failed to apply camera parameters");
  }

  return ret;
}

static gboolean
gst_droidcamsrc_start_image_capture_locked (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "start image capture");

  if (!gst_droidcamsrc_dev_capture_image (src->dev)) {
    GST_ERROR_OBJECT (src, "failed to start image capture");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_remove_eos_and_tag (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  if (*event && (GST_EVENT_TYPE (*event) == GST_EVENT_EOS
          || GST_EVENT_TYPE (*event) == GST_EVENT_TAG)) {
    GST_INFO_OBJECT (pad, "removed %" GST_PTR_FORMAT, *event);
    gst_event_unref (*event);
    *event = NULL;
  }

  return TRUE;
}

static gboolean
gst_droidcamsrc_start_video_recording_locked (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "start video recording");

  /* TODO: is is that safe to do this? the assumption is _loop () is sleeping */
  src->vidsrc->open_segment = TRUE;

  /*
   * remove EOS and old tags. camerabin will push the new tags after we
   * start recording.
   * As for EOS, it's not needed and there is a race condition somewhere in the pipeline
   * that causes recording to break. It seems that flush-stop can travel faster than eos
   * which needs to be debugged more.
   */
  gst_pad_sticky_events_foreach (src->vidsrc->pad, _remove_eos_and_tag, NULL);

  /* camerabin will push application tags when we return which will fail
   * because the pipeline is flushing so we just do it here
   */
  GST_DEBUG_OBJECT (src, "sending FLUSH_STOP");
  if (!gst_pad_push_event (src->vidsrc->pad, gst_event_new_flush_stop (TRUE))) {
    GST_ERROR_OBJECT (src, "failed to push FLUSH_STOP event");
  }

  if (!gst_droidcamsrc_dev_start_video_recording (src->dev)) {
    GST_ERROR_OBJECT (src, "failed to start video recording");
    return FALSE;
  }

  return TRUE;
}

static void
gst_droidcamsrc_stop_video_recording_locked (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "stop video recording");
  gst_droidcamsrc_dev_stop_video_recording (src->dev);
}

static void
gst_droidcamsrc_start_capture (GstDroidCamSrc * src)
{
  gboolean started;

  GST_DEBUG_OBJECT (src, "start capture");

  if (!src->dev) {
    GST_ELEMENT_WARNING (src, RESOURCE, FAILED, (NULL), (NULL));
    GST_ERROR_OBJECT (src, "cannot capture while not running");
    started = FALSE;
    goto out;
  }

  g_mutex_lock (&src->capture_lock);

  if (src->captures > 0) {
    /* abort if we are capturing */
    GST_ELEMENT_WARNING (src, RESOURCE, FAILED, (NULL), (NULL));
    started = FALSE;
    goto out;
  }

  /* This is needed to allow camerabin to switch filesink to PLAYING */

  ++src->captures;
  g_mutex_unlock (&src->capture_lock);
  g_object_notify (G_OBJECT (src), "ready-for-capture");
  g_mutex_lock (&src->capture_lock);

  if (src->mode == MODE_IMAGE) {
    started = gst_droidcamsrc_start_image_capture_locked (src);
  } else {
    started = gst_droidcamsrc_start_video_recording_locked (src);
    if (started) {
      started = gst_droidcamsrc_apply_params (src);
    }
  }

  if (!started) {
    /* post a warning so camerabin can fix itself up */
    GST_ELEMENT_WARNING (src, RESOURCE, FAILED, (NULL), (NULL));
    --src->captures;
    g_mutex_unlock (&src->capture_lock);
    g_object_notify (G_OBJECT (src), "ready-for-capture");
    return;
  }

out:
  g_mutex_unlock (&src->capture_lock);

  GST_DEBUG_OBJECT (src, "started capture");
}

static void
gst_droidcamsrc_stop_capture (GstDroidCamSrc * src)
{
  gboolean notify = FALSE;

  GST_DEBUG_OBJECT (src, "stop capture");

  g_mutex_lock (&src->capture_lock);

  if (src->captures == 0) {
    /* abort if we are capturing */
    GST_WARNING_OBJECT (src,
        "trying to stop capturing while nothing is being captured");
    goto out;
  }

  if (src->mode != MODE_IMAGE) {
    gst_droidcamsrc_stop_video_recording_locked (src);
    notify = TRUE;
  }

out:
  if (notify) {
    --src->captures;
  }

  g_mutex_unlock (&src->capture_lock);

  if (notify) {
    g_object_notify (G_OBJECT (src), "ready-for-capture");
  }

  GST_DEBUG_OBJECT (src, "stopped capture");
}

void
gst_droidcamsrc_post_message (GstDroidCamSrc * src, GstStructure * s)
{
  GstMessage *msg;

  GST_DEBUG_OBJECT (src, "sending %s message", gst_structure_get_name (s));

  msg = gst_message_new_element (GST_OBJECT (src), s);

  if (gst_element_post_message (GST_ELEMENT (src), msg) == FALSE) {
    GST_WARNING_OBJECT (src,
        "this element has no bus, therefore no message sent!");
  }
}

void
gst_droidcamsrc_timestamp (GstDroidCamSrc * src, GstBuffer * buffer)
{
  GstClockTime base_time, ts;
  GstClock *clock;

  GST_OBJECT_LOCK (src);
  clock = GST_ELEMENT_CLOCK (src);
  if (clock) {
    gst_object_ref (clock);
  }

  base_time = GST_ELEMENT_CAST (src)->base_time;
  GST_OBJECT_UNLOCK (src);

  if (!clock) {
    GST_WARNING_OBJECT (src, "cannot timestamp without a clock");
    return;
  }

  ts = gst_clock_get_time (clock) - base_time;

  gst_object_unref (clock);

  /* TODO: duration */
  GST_BUFFER_DTS (buffer) = ts;
  GST_BUFFER_PTS (buffer) = ts;

  GST_LOG_OBJECT (src,
      "timestamp %" GST_TIME_FORMAT "for buffer %" GST_PTR_FORMAT,
      GST_TIME_ARGS (ts), buffer);
}

void
gst_droidcamsrc_update_max_zoom (GstDroidCamSrc * src)
{
  int max_zoom;
  GParamSpecFloat *pspec;
  gfloat current_zoom = 0.0f;

  GST_DEBUG_OBJECT (src, "update max zoom");

  g_rec_mutex_lock (&src->dev_lock);

  if (!src->dev) {
    GST_DEBUG_OBJECT (src, "camera not yet initialized");
    goto out;
  }

  if (!src->dev->params) {
    GST_DEBUG_OBJECT (src, "camera not yet opened");
    goto out;
  }

  max_zoom = gst_droidcamsrc_params_get_int (src->dev->params, "max-zoom");
  if (max_zoom == -1) {
    GST_WARNING_OBJECT (src, "camera hardware does not know about max-zoom");
    goto out;
  }

  GST_DEBUG_OBJECT (src, "max zoom reported from HAL is %d", max_zoom);

  GST_OBJECT_LOCK (src);
  /* add 1 because android zoom starts from 0 while we start from 1 */
  src->max_zoom = max_zoom + 1;
  GST_OBJECT_UNLOCK (src);

  g_object_notify (G_OBJECT (src), "max-zoom");

  /* now update zoom pspec */
  pspec =
      G_PARAM_SPEC_FLOAT (g_object_class_find_property (G_OBJECT_GET_CLASS
          (src), "zoom"));
  GST_OBJECT_LOCK (src);
  pspec->maximum = src->max_zoom;
  GST_OBJECT_UNLOCK (src);
  GST_INFO_OBJECT (src, "max zoom set to %f", pspec->maximum);

  /* if current zoom more than max-zoom then adjust it */
  g_object_get (src, "zoom", &current_zoom, NULL);
  if (pspec->maximum < current_zoom) {
    GST_DEBUG_OBJECT (src, "current zoom level is too high: %f", current_zoom);
    g_object_set (src, "zoom", pspec->maximum, NULL);
  }

out:
  g_rec_mutex_unlock (&src->dev_lock);
}

static void
gst_droidcamsrc_apply_quirk (GstDroidCamSrc * src,
    const gchar * name, gboolean state)
{
  gst_droidcamsrc_quirks_apply (src->quirks, src, src->dev->info->direction,
      src->mode, name, state);
}

void
gst_droidcamsrc_apply_mode_settings (GstDroidCamSrc * src,
    GstDroidCamSrcApplyType type)
{
  GST_DEBUG_OBJECT (src, "apply mode settings");

  if (!src->dev || !src->dev->params) {
    GST_DEBUG_OBJECT (src, "cannot apply mode settings now");
    return;
  }

  /* apply focus */
  gst_droidcamsrc_photography_set_focus_to_droid (src);

  /* video torch */
  gst_droidcamsrc_photography_set_flash_to_droid (src);

  /* jpeg quality */
  gst_droidcamsrc_update_jpeg_quality (src);

  /* face detection quirk */
  gst_droidcamsrc_apply_quirk (src, "face-detection", src->face_detection);

  /* face detection */
  if (src->mode == MODE_VIDEO || !src->face_detection) {
    /* stop face detection */
    gst_droidcamsrc_dev_enable_face_detection (src->dev, FALSE);
  } else {
    /* start face detection */
    gst_droidcamsrc_dev_enable_face_detection (src->dev, TRUE);
  }

  /* image noise reduction */
  gst_droidcamsrc_apply_quirk (src, "image-noise-reduction",
      src->image_noise_reduction);

  /* ZSL quirk */
  gst_droidcamsrc_apply_quirk (src, "zsl",
      (src->image_mode & GST_DROIDCAMSRC_IMAGE_MODE_ZSL));

  /* HDR quirk */
  gst_droidcamsrc_apply_quirk (src, "hdr",
      (src->image_mode & GST_DROIDCAMSRC_IMAGE_MODE_HDR));

  if (type == SET_AND_APPLY) {
    gst_droidcamsrc_apply_params (src);
  }
}

static void
gst_droidcamsrc_update_ev_compensation_bounds (GstDroidCamSrc * src)
{
  GParamSpecFloat *min_pspec;
  GParamSpecFloat *max_pspec;
  gfloat step;
  int min;
  int max;
  gfloat new_val = 0.0;

  GST_DEBUG_OBJECT (src, "update ev compensation bounds");

  step =
      gst_droidcamsrc_params_get_float (src->dev->params,
      "exposure-compensation-step");

  if (step <= 0.0) {
    GST_WARNING_OBJECT (src, "failed to get exposure-compensation-step");
    return;
  }

  min =
      gst_droidcamsrc_params_get_int (src->dev->params,
      "min-exposure-compensation");
  max =
      gst_droidcamsrc_params_get_int (src->dev->params,
      "max-exposure-compensation");

  if (min == max) {
    GST_WARNING_OBJECT (src,
        "invalid exposure compensation bounds from HAL: %d, %d", min, max);
    return;
  }

  min_pspec =
      G_PARAM_SPEC_FLOAT (g_object_class_find_property (G_OBJECT_GET_CLASS
          (src), "min-ev-compensation"));
  max_pspec =
      G_PARAM_SPEC_FLOAT (g_object_class_find_property (G_OBJECT_GET_CLASS
          (src), "max-ev-compensation"));

  min_pspec->minimum = min * step;
  min_pspec->maximum = min_pspec->minimum;

  max_pspec->minimum = max * step;
  max_pspec->maximum = max_pspec->minimum;

  src->min_ev_compensation = min_pspec->minimum;
  src->max_ev_compensation = max_pspec->minimum;
  src->ev_step = step;

  GST_INFO_OBJECT (src, "updated ev compensation bounds to be from %f to %f",
      min_pspec->minimum, max_pspec->minimum);

  g_object_notify (G_OBJECT (src), "min-ev-compensation");
  g_object_notify (G_OBJECT (src), "max-ev-compensation");

  g_object_get (src, "ev-compensation", &new_val, NULL);

  if (new_val > src->max_ev_compensation || new_val < src->min_ev_compensation) {
    new_val =
        CLAMP (new_val, src->min_ev_compensation, src->max_ev_compensation);
    g_object_set (src, "ev-compensation", new_val, NULL);
  }
}

static void
gst_droidcamsrc_add_vfsrc_orientation_tag (GstDroidCamSrc * src)
{
  static gchar *tags[] = {
    "rotate-0",
    "rotate-90",
    "rotate-180",
    "rotate-270",
  };

  const gchar *orientation;
  GstTagList *taglist;

  GST_DEBUG_OBJECT (src, "add vfsrc orientation tag");

  orientation = tags[src->dev->info->orientation];

  taglist = gst_tag_list_new (GST_TAG_IMAGE_ORIENTATION, orientation, NULL);

  /* The lock is not really needed but for the sake of consistency */
  g_mutex_lock (&src->vfsrc->lock);
  src->vfsrc->pending_events = g_list_append (src->vfsrc->pending_events,
      gst_event_new_tag (taglist));
  g_mutex_unlock (&src->vfsrc->lock);

  GST_INFO_OBJECT (src, "added orientation tag event with orientation %s",
      orientation);
}

static gboolean
gst_droidcamsrc_select_and_activate_mode (GstDroidCamSrc * src)
{
  switch (src->mode) {
    case MODE_IMAGE:
      src->active_mode = src->image;
      break;

    case MODE_VIDEO:
      src->active_mode = src->video;
      break;

    default:
      GST_ERROR_OBJECT (src, "unknown mode");
      return FALSE;
  }

  if (!gst_droidcamsrc_mode_activate (src->active_mode)) {
    GST_ERROR_OBJECT (src, "failed to activate mode");
    return FALSE;
  }

  return TRUE;
}

static GstCaps *
gst_droidcamsrc_pick_largest_resolution (GstDroidCamSrc * src, GstCaps * caps)
{
  /* TODO: this function will fail if we have either width or height ranges */
  gint len;
  gint index = 0;
  gint mp = 0;
  gint x;
  GstCaps *out_caps;

  GST_LOG_OBJECT (src, "pick largest resolution from %" GST_PTR_FORMAT, caps);

  len = gst_caps_get_size (caps);

  if (len == 1) {
    return caps;
  }

  for (x = 0; x < len; x++) {
    gint width = 0, height = 0;
    GstStructure *s = gst_caps_get_structure (caps, x);
    gint size;

    if (!gst_structure_get_int (s, "width", &width) ||
        !gst_structure_get_int (s, "height", &height)) {
      continue;
    }

    size = width * height;

    if (size > mp) {
      mp = size;
      index = x;
    }
  }

  out_caps = gst_caps_copy_nth (caps, index);
  gst_caps_unref (caps);
  return out_caps;
}

static gchar *
gst_droidcamsrc_find_picture_resolution (GstDroidCamSrc * src,
    const gchar * resolution)
{
  const gchar *supported;
  gchar **vals = NULL, **tmp;
  gchar *ret = NULL;

  GST_DEBUG_OBJECT (src, "find picture resolution for %s", resolution);

  supported =
      gst_droidcamsrc_params_get_string (src->dev->params,
      "picture-size-values");

  GST_LOG_OBJECT (src, "supported picture sizes: %s", supported);

  vals = g_strsplit (supported, ",", -1);

  tmp = vals;
  while (*tmp) {
    if (!g_strcmp0 (*tmp, resolution)) {
      GST_DEBUG_OBJECT (src, "found resolution %s", *tmp);
      ret = g_strdup (*tmp);
      goto out;
    }
    ++tmp;
  }

out:
  g_strfreev (vals);

  if (!ret) {
    GST_WARNING_OBJECT (src, "no picture resolution corresponding to %s",
        resolution);
  }

  return ret;
}

static gboolean
gst_droidcamsrc_is_zsl_and_hdr_supported (GstDroidCamSrc * src)
{
  /*
   * There is no standard way to do our detection.
   * We will just follow some heuristics here
   */

  gboolean ret = FALSE;
  const gchar *str = NULL;

  GST_DEBUG_OBJECT (src, "is zsl and hdr supported");

  g_rec_mutex_lock (&src->dev_lock);
  if (!src->dev || !src->dev->params)
    goto out;

  str =
      gst_droidcamsrc_params_get_string (src->dev->params, "zsl-hdr-supported");
  if (!str)
    goto out;

  if (g_strcmp0 (str, "true") == 0)
    goto out;

out:
  g_rec_mutex_unlock (&src->dev_lock);

  if (str)
    g_free ((gpointer) str);

  GST_INFO_OBJECT (src, "zsl and hdr supported: %d", ret);
  return ret;
}

static GstCaps *
gst_droidcamsrc_get_video_caps_locked (GstDroidCamSrc * src)
{
  struct Data
  {
    GstCaps *result;
    GstStructure *encoded;
  };

  gboolean __map (GstCapsFeatures * features,
      GstStructure * structure, gpointer data_param)
  {

    /* Given structure from template caps, we need to transform Data::encoded */
    int i;
    struct Data *data = data_param;
    GstStructure *s = gst_structure_copy (data->encoded);
    for (i = 0; i < gst_structure_n_fields (structure); i++) {
      const gchar *field = gst_structure_nth_field_name (structure, i);
      /* skip format field */
      if (g_strcmp0 (field, "format") != 0) {
        gst_structure_set_value (s, field, gst_structure_get_value (structure,
                field));
      }
    }

    data->result = gst_caps_merge_structure (data->result, s);

    return TRUE;
  }

  GstCaps *tpl = gst_droidcamsrc_params_get_video_caps (src->dev->params);
  GstCaps *caps = gst_caps_new_empty ();
  GstCaps *encoded =
      gst_droid_codec_get_all_caps (GST_DROID_CODEC_ENCODER_VIDEO);
  int x;

  struct Data data;
  data.result = caps;

  for (x = 0; x < gst_caps_get_size (encoded); x++) {
    data.encoded = gst_caps_get_structure (encoded, x);
    gst_caps_foreach (tpl, __map, &data);
  }

  caps = gst_caps_merge (caps, tpl);

  caps = gst_caps_simplify (caps);

  gst_caps_unref (encoded);

  GST_DEBUG_OBJECT (src, "video caps %" GST_PTR_FORMAT, caps);

  return caps;
}

void
gst_droidcamsrc_post_preview (GstDroidCamSrc * src, GstSample * sample)
{
  if (src->post_preview) {
    gst_camerabin_preview_pipeline_post (src->preview_pipeline, sample);
  } else {
    GST_DEBUG_OBJECT (src, "Previews not enabled, not posting");
    gst_sample_unref (sample);
  }
}

static gboolean
gst_droidcamsrc_update_jpeg_quality(GstDroidCamSrc * src)
{
  gboolean ret = FALSE;
  gchar* str = NULL;

  GST_DEBUG_OBJECT (src, "update jpeg quality");

  do {
    gint jpeg_quality = 0;
    g_rec_mutex_lock (&src->dev_lock);

    if (!src->dev || !src->dev->params)
      break;

    jpeg_quality =
      gst_droidcamsrc_params_get_int (src->dev->params, "jpeg-quality");

    /*
     * Value is not good, keep compatibility with current devices
     * (-1 - value not present, 0 - value is not correct integer)
     */
    if (jpeg_quality <= 0)
      break;

    str = g_strdup_printf ("%u", src->jpeg_quality);
    gst_droidcamsrc_params_set_string(src->dev->params, "jpeg-quality", str);
    g_free(str);
    ret = TRUE;
  } while(0);

  g_rec_mutex_unlock (&src->dev_lock);

  GST_INFO_OBJECT (src, "setting jpeg quality to %u was %s", src->jpeg_quality,
    ret ? "OK" : "FAILED");
  return ret;
}
