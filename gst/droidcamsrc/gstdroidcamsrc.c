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
#include <gst/video/video.h>
#include <gst/memory/gstgralloc.h>
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#include <gst/basecamerabinsrc/gstbasecamerasrc.h>
#endif /* GST_USE_UNSTABLE_API */

#define gst_droidcamsrc_parent_class parent_class
G_DEFINE_TYPE (GstDroidCamSrc, gst_droidcamsrc, GST_TYPE_ELEMENT);

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

static GstStaticPadTemplate vf_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_SURFACE, "{ENCODED, YV12}")));

static GstStaticPadTemplate img_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("image/jpeg")));

// TODO: fix that when we know it
static GstStaticPadTemplate vid_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gboolean gst_droidcamsrc_pad_activate_mode (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active);
static gboolean gst_droidcamsrc_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_droidcamsrc_pad_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static GstDroidCamSrcPad *
gst_droidcamsrc_create_pad (GstDroidCamSrc * src, GstStaticPadTemplate * tpl,
    const gchar * name)
{
  GstDroidCamSrcPad *pad = g_slice_new0 (GstDroidCamSrcPad);

  pad->pad = gst_pad_new_from_static_template (tpl, name);
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
  pad->caps = NULL;

  gst_segment_init (&pad->segment, GST_FORMAT_TIME);

  gst_element_add_pad (GST_ELEMENT (src), pad->pad);

  return pad;
}

static void
gst_droidcamsrc_destroy_pad (GstDroidCamSrcPad * pad)
{
  /* we don't destroy the pad itself */
  if (pad->caps) {
    gst_caps_unref (pad->caps);
  }

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

  src->vfsrc = gst_droidcamsrc_create_pad (src,
      &vf_src_template_factory, GST_BASE_CAMERA_SRC_VIEWFINDER_PAD_NAME);
  src->imgsrc = gst_droidcamsrc_create_pad (src,
      &img_src_template_factory, GST_BASE_CAMERA_SRC_IMAGE_PAD_NAME);

  src->vidsrc = gst_droidcamsrc_create_pad (src,
      &vid_src_template_factory, GST_BASE_CAMERA_SRC_VIDEO_PAD_NAME);

  GST_OBJECT_FLAG_SET (src, GST_ELEMENT_FLAG_SOURCE);
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
      } else if (!gst_droidcamsrc_dev_init (src->dev)) {
        ret = GST_STATE_CHANGE_FAILURE;
      }

      /* our buffer pool will push buffers to the queue so it needs to know about it */
      src->dev->pool->pad = src->vfsrc;

      break;

    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (!gst_droidcamsrc_dev_start (src->dev)) {
        ret = GST_STATE_CHANGE_FAILURE;
      }

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
      gst_droidcamsrc_dev_stop (src->dev);
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

  gobject_class->finalize = gst_droidcamsrc_finalize;
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_droidcamsrc_change_state);
}

static void
gst_droidcamsrc_loop (gpointer user_data)
{
  GstFlowReturn ret;
  GstDroidCamSrcPad *data = (GstDroidCamSrcPad *) user_data;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (data->pad));
  GstBuffer *buffer = NULL;
  GstCaps *caps = NULL, *current = NULL;

  GST_LOG_OBJECT (src, "loop %s", GST_PAD_NAME (data->pad));

  g_mutex_lock (&data->lock);
  if (!data->running) {
    GST_DEBUG_OBJECT (src, "task is not running");
    goto unlock_and_out;
  }

  buffer = g_queue_pop_head (data->queue);
  if (buffer) {
    caps = gst_caps_ref (data->caps);
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
    caps = gst_caps_ref (data->caps);
    g_mutex_unlock (&data->lock);
    goto out;
  }


unlock_and_out:
  g_mutex_unlock (&data->lock);
  return;

out:
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

  /* caps */
  current = gst_pad_get_current_caps (data->pad);

  if (!current || !gst_caps_is_equal (caps, current)) {
    GST_DEBUG_OBJECT (src, "current caps for pad %s: %" GST_PTR_FORMAT,
        GST_PAD_NAME (data->pad), current);
    if (!gst_pad_set_caps (data->pad, caps)) {
      GST_ERROR_OBJECT (src, "failed to set caps");
    } else {
      // TODO: what should we really do here?
      gst_pad_check_reconfigure (data->pad);
    }
  }

  /* segment */
  if (G_UNLIKELY (data->open_segment)) {
    GstEvent *event;

    GST_DEBUG_OBJECT (src, "Pushing SEGMENT");

    // TODO: consider buffer timestamp as start?
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

  if (caps) {
    gst_caps_unref (caps);
  }

  if (current) {
    gst_caps_unref (current);
  }
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
    if (data->caps) {
      gst_caps_unref (data->caps);
      data->caps = NULL;
    }
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
  //  GstDroidCamSrcPad *data = gst_pad_get_element_private (pad);

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
    case GST_EVENT_RECONFIGURE:
      // TODO:
      ret = TRUE;
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
      GstCaps *caps = NULL;
      gst_query_parse_accept_caps (query, &caps);
      g_mutex_lock (&data->lock);
      if (caps && gst_caps_is_equal (caps, data->caps)) {
        gst_query_set_accept_caps_result (query, TRUE);
      } else {
        gst_query_set_accept_caps_result (query, FALSE);
      }
      g_mutex_unlock (&data->lock);

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
      g_mutex_lock (&data->lock);
      if (data->caps) {
        gst_query_set_caps_result (query, data->caps);
        ret = TRUE;
      } else {
        // TODO: should we add the pad template caps ?
        ret = FALSE;
      }

      g_mutex_unlock (&data->lock);

      break;
  }

  if (ret) {
    GST_LOG_OBJECT (src, "replying to %" GST_PTR_FORMAT, query);
  } else {
    GST_LOG_OBJECT (src, "discarding %" GST_PTR_FORMAT, query);
  }

  return ret;
}
