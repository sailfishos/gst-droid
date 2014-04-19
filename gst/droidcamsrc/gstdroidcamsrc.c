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

#include "plugin.h"
#include "gstdroidcamsrc.h"
#include <gst/video/video.h>
#include <gst/memory/gstgralloc.h>
#include <gst/memory/gstwrappedmemory.h>
#include "gstdroidcamsrcphotography.h"
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include <gst/interfaces/photography.h>

#define gst_droidcamsrc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDroidCamSrc, gst_droidcamsrc, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_PHOTOGRAPHY,
        gst_droidcamsrc_photography_register));

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

static GstStaticPadTemplate vf_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_HANDLE, "{ENCODED, YV12}")));

static GstStaticPadTemplate img_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg"));

static GstStaticPadTemplate vid_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_VIDEO_META_DATA, "{ENCODED, YV12}")));

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
static void gst_droidcamsrc_update_max_zoom (GstDroidCamSrc * src);
static void gst_droidcamsrc_apply_mode_settings (GstDroidCamSrc * src,
    GstDroidCamSrcPhotographyApplyType type);

enum
{
  /* action signals */
  START_CAPTURE_SIGNAL,
  STOP_CAPTURE_SIGNAL,
  /* emit signals */
  LAST_SIGNAL
};

static guint droidcamsrc_signals[LAST_SIGNAL];

#define DEFAULT_CAMERA_DEVICE GST_DROIDCAMSRC_CAMERA_DEVICE_PRIMARY
#define DEFAULT_MODE          MODE_IMAGE
#define DEFAULT_MAX_ZOOM      10.0f
#define DEFAULT_VIDEO_TORCH   FALSE

static GstDroidCamSrcPad *
gst_droidcamsrc_create_pad (GstDroidCamSrc * src, GstStaticPadTemplate * tpl,
    const gchar * name, gboolean capture_pad)
{
  GstDroidCamSrcPad *pad = g_slice_new0 (GstDroidCamSrcPad);

  pad->pad = gst_pad_new_from_static_template (tpl, name);

  // TODO: I don't think this is needed
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
  pad->send_flush_stop = FALSE;
  pad->negotiate = NULL;
  pad->capture_pad = capture_pad;
  pad->pushed_buffers = 0;
  pad->adjust_segment = FALSE;
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
  src->hw = NULL;
  src->dev = NULL;
  src->camera_device = DEFAULT_CAMERA_DEVICE;
  src->user_camera_device = DEFAULT_CAMERA_DEVICE;
  src->mode = DEFAULT_MODE;
  src->captures = 0;
  g_mutex_init (&src->capture_lock);
  src->max_zoom = DEFAULT_MAX_ZOOM;
  src->video_torch = DEFAULT_VIDEO_TORCH;

  gst_droidcamsrc_photography_init (src);

  src->vfsrc = gst_droidcamsrc_create_pad (src,
      &vf_src_template_factory, GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME, FALSE);
  src->vfsrc->negotiate = gst_droidcamsrc_vfsrc_negotiate;

  src->imgsrc = gst_droidcamsrc_create_pad (src,
      &img_src_template_factory, GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME, TRUE);
  src->imgsrc->negotiate = gst_droidcamsrc_imgsrc_negotiate;

  src->vidsrc = gst_droidcamsrc_create_pad (src,
      &vid_src_template_factory, GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME, TRUE);
  src->vidsrc->adjust_segment = TRUE;
  src->vidsrc->negotiate = gst_droidcamsrc_vidsrc_negotiate;

  GST_OBJECT_FLAG_SET (src, GST_ELEMENT_FLAG_SOURCE);
}

static void
gst_droidcamsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (object);

  if (gst_droidcamsrc_photography_get_property (src, prop_id, value, pspec)) {
    return;
  }

  switch (prop_id) {
    case PROP_CAMERA_DEVICE:
      g_value_set_enum (value, src->camera_device);
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
      src->user_camera_device = g_value_get_enum (value);
      GST_DEBUG_OBJECT (src, "camera device set to %d",
          src->user_camera_device);
      break;

    case PROP_MODE:
    {
      GstCameraBinMode mode = g_value_get_enum (value);

      GST_INFO_OBJECT (src, "setting capture mode to : %d", mode);

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

      /* apply mode settings */
      gst_droidcamsrc_apply_mode_settings (src, GST_PHOTO_SET_AND_APPLY);
    }

      break;

    case PROP_VIDEO_TORCH:
      /* set value */
      src->video_torch = g_value_get_boolean (value);

      /* apply */
      gst_droidcamsrc_apply_mode_settings (src, GST_PHOTO_SET_AND_APPLY);
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

  gst_droidcamsrc_destroy_pad (src->vfsrc);
  gst_droidcamsrc_destroy_pad (src->imgsrc);
  gst_droidcamsrc_destroy_pad (src->vidsrc);

  g_mutex_clear (&src->capture_lock);

  gst_droidcamsrc_photography_destroy (src);

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
      target->direction =
          facing ==
          CAMERA_FACING_FRONT ? NEMO_GST_META_DEVICE_DIRECTION_FRONT :
          NEMO_GST_META_DEVICE_DIRECTION_BACK;
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

static GstDroidCamSrcCamInfo *
gst_droidcamsrc_find_camera_device (GstDroidCamSrc * src)
{
  int x;
  NemoGstDeviceDirection direction =
      src->camera_device ==
      GST_DROIDCAMSRC_CAMERA_DEVICE_SECONDARY ?
      NEMO_GST_META_DEVICE_DIRECTION_FRONT :
      NEMO_GST_META_DEVICE_DIRECTION_BACK;

  for (x = 0; x < MAX_CAMERAS; x++) {
    if (src->info[x].direction == direction) {
      return &src->info[x];
    }
  }

  GST_ERROR_OBJECT (src, "cannot find camera %d", src->camera_device);

  return NULL;
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
        break;
      }

      src->dev =
          gst_droidcamsrc_dev_new (src->hw, src->vfsrc, src->imgsrc,
          src->vidsrc);

      break;

    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      /* find the device */
      gboolean res;
      GstDroidCamSrcCamInfo *info;
      src->camera_device = src->user_camera_device;
      info = gst_droidcamsrc_find_camera_device (src);

      if (!info) {
        ret = GST_STATE_CHANGE_FAILURE;
        break;
      }

      GST_DEBUG_OBJECT (src, "using camera device %i", info->num);

      res = gst_droidcamsrc_dev_open (src->dev, info);
      if (!res) {
        ret = GST_STATE_CHANGE_FAILURE;
        break;
      } else if (!gst_droidcamsrc_dev_init (src->dev)) {
        ret = GST_STATE_CHANGE_FAILURE;
        break;
      }
    }

      break;

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* set initial photography parameters */
      gst_droidcamsrc_photography_apply (src, GST_PHOTO_SET_ONLY);

      /* apply mode settings */
      gst_droidcamsrc_apply_mode_settings (src, GST_PHOTO_SET_ONLY);

      /* now start */
      if (!gst_droidcamsrc_dev_start (src->dev)) {
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
      // TODO: stop recording if we are recording
      gst_droidcamsrc_dev_stop (src->dev);
      src->captures = 0;

      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_droidcamsrc_dev_deinit (src->dev);
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

  if (ret == GST_STATE_CHANGE_SUCCESS
      && (transition == GST_STATE_CHANGE_READY_TO_PAUSED
          || transition == GST_STATE_CHANGE_PLAYING_TO_PAUSED)) {
    ret = GST_STATE_CHANGE_NO_PREROLL;
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

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&vf_src_template_factory));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&img_src_template_factory));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&vid_src_template_factory));

  gobject_class->set_property = gst_droidcamsrc_set_property;
  gobject_class->get_property = gst_droidcamsrc_get_property;
  gobject_class->finalize = gst_droidcamsrc_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_droidcamsrc_change_state);

  g_object_class_install_property (gobject_class, PROP_CAMERA_DEVICE,
      g_param_spec_enum ("camera-device", "Camera device",
          "Defines which camera device should be used",
          GST_TYPE_DROIDCAMSRC_CAMERA_DEVICE,
          DEFAULT_CAMERA_DEVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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

  g_object_class_install_property (gobject_class, PROP_VIDEO_TORCH,
      g_param_spec_boolean ("video-torch", "Video torch",
          "Sets torch light on or off for video recording", DEFAULT_VIDEO_TORCH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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

  GST_LOG_OBJECT (src, "loop %s", GST_PAD_NAME (data->pad));

  g_mutex_lock (&data->lock);
  if (!data->running) {
    GST_DEBUG_OBJECT (src, "task is not running");
    goto unlock_and_out;
  }

  /* stream start */
  if (G_UNLIKELY (data->open_stream)) {
    gchar *stream_id;
    GstEvent *event;

    stream_id =
        gst_pad_create_stream_id (data->pad, GST_ELEMENT_CAST (src),
        GST_PAD_NAME (data->pad));

    GST_DEBUG_OBJECT (src, "Pushing STREAM_START");
    event = gst_event_new_stream_start (stream_id);
    gst_event_set_group_id (event, gst_util_group_id_next ());
    if (!gst_pad_push_event (data->pad, event)) {
      GST_ERROR_OBJECT (src, "failed to push STREAM_START event");
    }

    g_free (stream_id);
    data->open_stream = FALSE;
  }

  if (G_UNLIKELY (gst_pad_check_reconfigure (data->pad))) {
    gboolean res = FALSE;

    GST_DEBUG_OBJECT (src, "pad %s needs negotiation",
        GST_PAD_NAME (data->pad));
    res = data->negotiate (data);

    if (!res) {
      GST_ELEMENT_ERROR (src, STREAM, FORMAT, (NULL),
          ("failed to negotiate %s.", GST_PAD_NAME (data->pad)));
      goto error;
    }

    /* toss our queue */
    g_queue_foreach (data->queue, (GFunc) gst_buffer_unref, NULL);
    g_queue_clear (data->queue);
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

  if (!buffer) {
    /* we got signaled to exit */
    goto unlock_and_out;
  } else {
    g_mutex_unlock (&data->lock);
    goto out;
  }

  return;
error:
  gst_pad_pause_task (data->pad);

unlock_and_out:
  g_mutex_unlock (&data->lock);
  return;

out:
  if (G_UNLIKELY (data->send_flush_stop)) {
    GST_DEBUG_OBJECT (src, "sending FLUSH_STOP");

    if (!gst_pad_push_event (data->pad, gst_event_new_flush_stop (TRUE))) {
      GST_ERROR_OBJECT (src, "failed to push FLUSH_STOP event");
    }

    data->send_flush_stop = FALSE;
  }

  /* segment */
  if (G_UNLIKELY (data->open_segment)) {
    GstEvent *event;

    GST_DEBUG_OBJECT (src, "Pushing SEGMENT");

    if (data->adjust_segment) {
      data->segment.start = GST_BUFFER_TIMESTAMP (buffer);
    }

    event = gst_event_new_segment (&data->segment);

    if (!gst_pad_push_event (data->pad, event)) {
      GST_ERROR_OBJECT (src, "failed to push SEGMENT event");
    }

    data->open_segment = FALSE;
  }

  /* finally we can push our buffer */
  ret = gst_pad_push (data->pad, buffer);

  if (ret != GST_FLOW_OK) {
    // TODO:
    GST_ERROR_OBJECT (src, "error %s pushing buffer through pad %s",
        gst_flow_get_name (ret), GST_PAD_NAME (data->pad));
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

  g_mutex_lock (&data->lock);
  data->running = active;
  g_cond_signal (&data->cond);
  g_mutex_unlock (&data->lock);

  if (active) {
    // TODO: review locking for the remaining 2 pads
    /* No need for locking here since the task is not running */
    data->open_stream = TRUE;
    data->open_segment = TRUE;
    data->pushed_buffers = 0;
    if (!gst_pad_start_task (pad, gst_droidcamsrc_loop, data, NULL)) {
      GST_ERROR_OBJECT (src, "failed to start pad task");
      return FALSE;
    }
  } else {
    gboolean ret = FALSE;
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

  GST_DEBUG_OBJECT (src, "pad %s %" GST_PTR_FORMAT, GST_PAD_NAME (pad), event);

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
      ret = FALSE;
      break;

    case GST_EVENT_CAPS:
    case GST_EVENT_LATENCY:
      ret = TRUE;
      // TODO:
      break;

    case GST_EVENT_RECONFIGURE:
      g_mutex_lock (&src->capture_lock);
      if (data->capture_pad && src->captures > 0) {
        GST_ERROR_OBJECT (src, "cannot reconfigure pad %s while capturing",
            GST_PAD_NAME (data->pad));
        ret = FALSE;
      } else if (data->capture_pad) {
        /* wake pad up to renegotiate */
        g_mutex_lock (&data->lock);
        g_cond_signal (&data->cond);
        g_mutex_unlock (&data->lock);
        ret = TRUE;
      } else {
        /* pad will negotiate later */
        ret = TRUE;
      }

      g_mutex_unlock (&src->capture_lock);

      break;

    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
      ret = TRUE;
      // TODO: what do we do here?
      break;
  }

  if (ret) {
    GST_LOG_OBJECT (src, "replying to %" GST_PTR_FORMAT, event);
  } else {
    GST_LOG_OBJECT (src, "discarding %" GST_PTR_FORMAT, event);
  }

  return ret;
}

static gboolean
gst_droidcamsrc_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (parent);
  gboolean ret = FALSE;
  GstDroidCamSrcPad *data = gst_pad_get_element_private (pad);
  GstCaps *caps = NULL;
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
    case GST_QUERY_CUSTOM:
    case GST_QUERY_ALLOCATION:
    case GST_QUERY_SEGMENT:
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
      // TODO: this assummes 7 buffers @ 30 FPS. Query from either bufferpool or camera params
      gst_query_set_latency (query, TRUE, 33, 33 * 7);
      ret = TRUE;
      break;


    case GST_QUERY_CAPS:
      caps = gst_pad_get_pad_template_caps (data->pad);
      if (caps) {
        gst_query_set_caps_result (query, caps);
        ret = TRUE;
      } else {
        caps =
            gst_caps_make_writable (gst_pad_get_pad_template_caps (data->pad));
        gst_query_set_caps_result (query, caps);
        ret = TRUE;
      }

      gst_caps_unref (caps);
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
  gchar *preview;
  GstVideoInfo info;

  GST_DEBUG_OBJECT (src, "vfsrc negotiate");

  our_caps = gst_droidcamsrc_params_get_viewfinder_caps (src->dev->params);
  GST_DEBUG_OBJECT (src, "our caps %" GST_PTR_FORMAT, our_caps);

  if (!our_caps || gst_caps_is_empty (our_caps)) {
    GST_ERROR_OBJECT (src, "no caps");
    goto out;
  }

  peer = gst_pad_peer_query_caps (data->pad, our_caps);
  GST_DEBUG_OBJECT (src, "peer caps %" GST_PTR_FORMAT, peer);

  if (!peer || gst_caps_is_empty (peer)) {
    GST_ERROR_OBJECT (src, "no common caps");
    goto out;
  }

  gst_caps_unref (our_caps);
  our_caps = peer;
  peer = NULL;

  our_caps = gst_caps_make_writable (our_caps);
  our_caps = gst_caps_truncate (our_caps);

  if (!gst_pad_set_caps (data->pad, our_caps)) {
    GST_ERROR_OBJECT (src, "failed to set caps");
    goto out;
  }

  GST_DEBUG_OBJECT (src, "pad %s using caps %" GST_PTR_FORMAT,
      GST_PAD_NAME (data->pad), our_caps);

  if (!gst_video_info_from_caps (&info, our_caps)) {
    GST_ERROR_OBJECT (src, "failed to parse caps");
    goto out;
  }

  preview = g_strdup_printf ("%ix%i", info.width, info.height);
  if (!gst_droidcamsrc_params_set_string (src->dev->params, "preview-size",
          preview)) {
    g_free (preview);
    GST_ERROR_OBJECT (src, "failed to set preview-size");
    goto out;
  }

  g_free (preview);

  if (!gst_droidcamsrc_apply_params (src)) {
    goto out;
  }

  ret = TRUE;

out:
  if (peer) {
    gst_caps_unref (peer);
  }

  if (our_caps) {
    gst_caps_unref (our_caps);
  }

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

  GST_DEBUG_OBJECT (src, "imgsrc negotiate");

  our_caps = gst_droidcamsrc_params_get_image_caps (src->dev->params);
  GST_DEBUG_OBJECT (src, "our caps %" GST_PTR_FORMAT, our_caps);

  if (!our_caps || gst_caps_is_empty (our_caps)) {
    GST_ERROR_OBJECT (src, "no caps");
    goto out;
  }

  peer = gst_pad_peer_query_caps (data->pad, our_caps);
  GST_DEBUG_OBJECT (src, "peer caps %" GST_PTR_FORMAT, peer);

  if (!peer || gst_caps_is_empty (peer)) {
    GST_ERROR_OBJECT (src, "no common caps");
    goto out;
  }

  gst_caps_unref (our_caps);
  our_caps = peer;
  peer = NULL;

  our_caps = gst_caps_make_writable (our_caps);
  our_caps = gst_caps_truncate (our_caps);

  if (!gst_pad_set_caps (data->pad, our_caps)) {
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
  if (!gst_droidcamsrc_params_set_string (src->dev->params, "picture-size",
          pic)) {
    g_free (pic);
    GST_ERROR_OBJECT (src, "failed to set picture-size");
    goto out;
  }

  g_free (pic);

  if (!gst_droidcamsrc_apply_params (src)) {
    goto out;
  }

  ret = TRUE;

out:
  if (peer) {
    gst_caps_unref (peer);
  }

  if (our_caps) {
    gst_caps_unref (our_caps);
  }

  return ret;
}

static gboolean
gst_droidcamsrc_vidsrc_negotiate (GstDroidCamSrcPad * data)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (data->pad));
  gboolean ret = FALSE;
  GstCaps *peer = NULL;
  GstCaps *our_caps = NULL;
  gchar *vid;
  GstVideoInfo info;

  GST_DEBUG_OBJECT (src, "vidsrc negotiate");

  our_caps = gst_droidcamsrc_params_get_video_caps (src->dev->params);
  GST_DEBUG_OBJECT (src, "our caps %" GST_PTR_FORMAT, our_caps);

  if (!our_caps || gst_caps_is_empty (our_caps)) {
    GST_ERROR_OBJECT (src, "no caps");
    goto out;
  }

  peer = gst_pad_peer_query_caps (data->pad, our_caps);
  GST_DEBUG_OBJECT (src, "peer caps %" GST_PTR_FORMAT, peer);

  if (!peer || gst_caps_is_empty (peer)) {
    GST_ERROR_OBJECT (src, "no common caps");
    goto out;
  }

  gst_caps_unref (our_caps);
  our_caps = peer;
  peer = NULL;

  our_caps = gst_caps_make_writable (our_caps);
  our_caps = gst_caps_truncate (our_caps);

  if (!gst_pad_set_caps (data->pad, our_caps)) {
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
  if (!gst_droidcamsrc_params_set_string (src->dev->params, "video-size", vid)) {
    g_free (vid);
    GST_ERROR_OBJECT (src, "failed to set video-size");
    goto out;
  }

  g_free (vid);

  if (!gst_droidcamsrc_apply_params (src)) {
    goto out;
  }

  /* now update max-zoom that we have a preview size */
  gst_droidcamsrc_dev_update_params (src->dev);
  gst_droidcamsrc_update_max_zoom (src);

  ret = TRUE;

out:
  if (peer) {
    gst_caps_unref (peer);
  }

  if (our_caps) {
    gst_caps_unref (our_caps);
  }

  return ret;
}

gboolean
gst_droidcamsrc_apply_params (GstDroidCamSrc * src)
{
  gchar *params;
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (src, "apply params");

  params = gst_droidcamsrc_params_to_string (src->dev->params);
  ret = gst_droidcamsrc_dev_set_params (src->dev, params);
  g_free (params);

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
gst_droidcamsrc_start_video_recording_locked (GstDroidCamSrc * src)
{
  GST_DEBUG_OBJECT (src, "start video recording");

  /* TODO: is is that safe to do this? the assumption is _loop () is sleeping */
  src->vidsrc->open_segment = TRUE;
  src->vidsrc->send_flush_stop = TRUE;

  if (!gst_droidcamsrc_dev_start_video_recording (src->dev)) {
    GST_ERROR_OBJECT (src, "failed to start image capture");
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

  if (src->mode == MODE_IMAGE) {
    started = gst_droidcamsrc_start_image_capture_locked (src);
  } else {
    started = gst_droidcamsrc_start_video_recording_locked (src);
  }

  if (!started) {
    GST_ELEMENT_WARNING (src, RESOURCE, FAILED, (NULL), (NULL));
  } else {
    ++src->captures;
  }

out:
  g_mutex_unlock (&src->capture_lock);

  if (started) {
    g_object_notify (G_OBJECT (src), "ready-for-capture");
  }
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

  GST_DEBUG_OBJECT (src, "timestamp %p", buffer);

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

  // TODO: duration
  GST_BUFFER_DTS (buffer) = ts;
  GST_BUFFER_PTS (buffer) = ts;
}

static void
gst_droidcamsrc_update_max_zoom (GstDroidCamSrc * src)
{
  int max_zoom;
  GParamSpecFloat *pspec;
  gfloat current_zoom = 0.0f;

  GST_DEBUG_OBJECT (src, "update max zoom");

  if (!src->dev) {
    GST_DEBUG_OBJECT (src, "camera not yet initialized");
    return;
  }
  // TODO: there is no lock for accesing the dev so dev can go
  // away by the time we attempt to lock it
  g_mutex_lock (&src->dev->lock);

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
  g_mutex_unlock (&src->dev->lock);
}

static void
gst_droidcamsrc_apply_mode_settings (GstDroidCamSrc * src,
    GstDroidCamSrcPhotographyApplyType type)
{
  GST_DEBUG_OBJECT (src, "apply mode settings");

  if (!src->dev || !src->dev->params) {
    GST_DEBUG_OBJECT (src, "cannot apply mode settings now");
    return;
  }

  /* apply focus */
  gst_droidcamsrc_photography_set_focus (src);

  /* video torch */
  gst_droidcamsrc_photography_set_flash (src);

  /* apply denoising */
  // TODO:

  if (type == GST_PHOTO_SET_AND_APPLY) {
    gst_droidcamsrc_apply_params (src);
  }
}
