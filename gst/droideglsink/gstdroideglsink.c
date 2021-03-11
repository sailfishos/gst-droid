/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer
 * Copyright (C) 2015-2020 Jolla Ltd.
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

#include "gstdroideglsink.h"
#include <gst/video/video.h>
#include <gst/interfaces/nemovideotexture.h>
#include "gst/droid/gstdroidmediabuffer.h"
#include "gst/droid/gstdroidbufferpool.h"

/* Element signals and args */
enum
{
  SHOW_FRAME,
  FLUSH,
  BUFFERS_INVALIDATED,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_EGL_DISPLAY
};

GST_DEBUG_CATEGORY_EXTERN (gst_droid_eglsink_debug);
#define GST_CAT_DEFAULT gst_droid_eglsink_debug

#define gst_droideglsink_parent_class parent_class
G_DEFINE_TYPE (GstDroidEglSink, gst_droideglsink, GST_TYPE_VIDEO_SINK);

static guint gst_droideglsink_signals[LAST_SIGNAL] = { 0 };

static GstStaticPadTemplate gst_droideglsink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_BUFFER,
            GST_DROID_MEDIA_BUFFER_MEMORY_VIDEO_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_QUEUE_BUFFER,
            GST_DROID_MEDIA_BUFFER_MEMORY_VIDEO_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE (GST_DROID_MEDIA_BUFFER_MEMORY_VIDEO_FORMATS)));

static void gst_droideglsink_buffer_pool_invalidated (GstBufferPool * pool,
    GstDroidEglSink * sink);
static void gst_droideglsink_buffers_invalidated (GstDroidEglSink * sink);

static GstCaps *
gst_droideglsink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstDroidEglSink *sink;
  GstCaps *caps;

  sink = GST_DROIDEGLSINK (bsink);

  GST_DEBUG_OBJECT (sink, "get caps called with filter caps %" GST_PTR_FORMAT,
      filter);
  caps = gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD (sink));

  if (filter) {
    GstCaps *intersection;
    intersection =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }

  GST_DEBUG_OBJECT (bsink, "returning caps %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_droideglsink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstDroidEglSink *sink;
  GstVideoSink *vsink;
  GstVideoInfo info;

  sink = GST_DROIDEGLSINK (bsink);
  vsink = GST_VIDEO_SINK (bsink);

  GST_DEBUG_OBJECT (sink, "set caps with %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ELEMENT_ERROR (sink, STREAM, FORMAT, (NULL),
        ("Could not locate image format from caps %" GST_PTR_FORMAT, caps));
    return FALSE;
  }

  GST_VIDEO_SINK_WIDTH (vsink) = info.width;
  GST_VIDEO_SINK_HEIGHT (vsink) = info.height;

  return TRUE;
}

static gboolean
gst_droideglsink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstDroidEglSink *sink = GST_DROIDEGLSINK (bsink);
  GstBufferPool *previous_pool = NULL;
  gulong previous_pool_signal_id = 0;
  GstCaps *caps;
  guint size;
  gboolean need_pool;
  gboolean queue_pool = FALSE;
  GstVideoInfo video_info;
  gboolean ret = FALSE;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (!caps) {
    GST_ERROR_OBJECT (sink, "No query caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&video_info, caps)) {
    GST_ERROR_OBJECT (sink, "Unable to get video caps");
    return FALSE;
  }

  g_mutex_lock (&sink->lock);

  previous_pool = sink->pool;
  previous_pool_signal_id = sink->invalidated_signal_id;

  sink->pool = NULL;
  sink->invalidated_signal_id = 0;

  if (need_pool) {
    GstBufferPool *pool = NULL;
    GstStructure *config;
    guint min = 2;
    guint max = 0;
    GstCapsFeatures *features = gst_caps_get_features (caps, 0);

    if (gst_caps_features_contains
        (features, GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_QUEUE_BUFFER)) {
      min = 0;
      max = droid_media_buffer_queue_length ();
      queue_pool = true;
    }

    size = video_info.finfo->format == GST_VIDEO_FORMAT_ENCODED
        ? 1 : video_info.size;

    if (previous_pool) {
      GstCaps *pool_caps;

      config = gst_buffer_pool_get_config (previous_pool);
      if (config
          && gst_buffer_pool_config_get_params (config, &pool_caps, NULL, NULL,
              NULL) && gst_caps_is_equal (caps, pool_caps)) {
        gst_buffer_pool_config_set_params (config, caps, size, min, max);

        if (gst_buffer_pool_set_config (previous_pool, config)) {
          pool = previous_pool;
          sink->invalidated_signal_id = previous_pool_signal_id;

          previous_pool = NULL;
        }
      }
    }

    if (!pool) {
      pool = gst_droid_buffer_pool_new ();

      gst_droid_buffer_pool_set_egl_display (pool, sink->dpy);

      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_set_params (config, caps, size, min, max);
      if (!gst_buffer_pool_set_config (pool, config)) {
        GST_ERROR_OBJECT (sink, "Failed to set buffer pool configuration");
        gst_object_unref (pool);
        goto out;
      }

      if (queue_pool) {
        sink->invalidated_signal_id =
            g_signal_connect (pool, "buffers-invalidated",
            G_CALLBACK (gst_droideglsink_buffer_pool_invalidated), sink);
      }
    }

    gst_query_add_allocation_pool (query, pool, size, min,
        min > max ? min : max);

    sink->pool = pool;
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  GST_DEBUG_OBJECT (sink, "proposed allocation");

  ret = TRUE;

out:
  g_mutex_unlock (&sink->lock);

  if (previous_pool) {
    GST_DEBUG_OBJECT (sink, "freed previous pool\n");
    g_signal_handler_disconnect (previous_pool, previous_pool_signal_id);

    gst_buffer_pool_set_flushing (previous_pool, TRUE);
    gst_object_unref (previous_pool);

    gst_droideglsink_buffers_invalidated (sink);
  }


  return ret;
}

static GstFlowReturn
gst_droideglsink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  g_signal_emit (vsink, gst_droideglsink_signals[SHOW_FRAME], 0, buf);

  return GST_FLOW_OK;
}

static void
gst_droideglsink_buffers_invalidated (GstDroidEglSink * sink)
{
  g_signal_emit (sink, gst_droideglsink_signals[BUFFERS_INVALIDATED], 0);
}

void
gst_droideglsink_buffer_pool_invalidated (GstBufferPool * pool,
    GstDroidEglSink * sink)
{
  GstDroidEglSinkClass *klass;

  klass = GST_DROIDEGLSINK_GET_CLASS (sink);

  if (klass->buffers_invalidated) {
    klass->buffers_invalidated (sink);
  }
}

static void
gst_droideglsink_free_pool (GstDroidEglSink * sink)
{
  if (sink->pool) {
    if (sink->invalidated_signal_id != 0) {
      g_signal_handler_disconnect (sink->pool, sink->invalidated_signal_id);
      sink->invalidated_signal_id = 0;
    }
    gst_buffer_pool_set_flushing (sink->pool, TRUE);
    gst_object_unref (sink->pool);
    sink->pool = NULL;
  }
}

static GstStateChangeReturn
gst_droideglsink_change_state (GstElement * element, GstStateChange transition)
{
  GstDroidEglSink *sink;
  GstStateChangeReturn ret;

  sink = GST_DROIDEGLSINK (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_SUCCESS) {
    switch (transition) {
      case GST_STATE_CHANGE_PAUSED_TO_READY:
        gst_droideglsink_show_frame (GST_VIDEO_SINK (element), NULL);
        gst_droideglsink_free_pool (sink);
        break;
      default:
        break;
    }
  }

  return ret;
}

static void
gst_droideglsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDroidEglSink *sink;

  g_return_if_fail (GST_IS_DROIDEGLSINK (object));

  sink = GST_DROIDEGLSINK (object);

  switch (prop_id) {
    case PROP_EGL_DISPLAY:
      g_mutex_lock (&sink->lock);
      sink->dpy = g_value_get_pointer (value);
      GST_DROIDEGLSINK (sink)->dpy = sink->dpy;
      g_mutex_unlock (&sink->lock);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_droideglsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDroidEglSink *sink;

  g_return_if_fail (GST_IS_DROIDEGLSINK (object));

  sink = GST_DROIDEGLSINK (object);

  switch (prop_id) {
    case PROP_EGL_DISPLAY:
      g_mutex_lock (&sink->lock);
      g_value_set_pointer (value, sink->dpy);
      g_mutex_unlock (&sink->lock);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_droideglsink_finalize (GObject * object)
{
  GstDroidEglSink *sink = GST_DROIDEGLSINK (object);

  gst_droideglsink_free_pool (sink);

  g_mutex_clear (&sink->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_droideglsink_init (GstDroidEglSink * sink)
{
  sink->pool = NULL;
  sink->dpy = EGL_NO_DISPLAY;
  g_mutex_init (&sink->lock);
}

static void
gst_droideglsink_class_init (GstDroidEglSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *videosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  videosink_class = (GstVideoSinkClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Video sink", "Sink/Video/Device",
      "EGL based videosink", "Mohammed Sameer <msameer@foolab.org>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droideglsink_sink_template_factory));

  gobject_class->set_property = gst_droideglsink_set_property;
  gobject_class->get_property = gst_droideglsink_get_property;
  gobject_class->finalize = gst_droideglsink_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_droideglsink_change_state);

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_droideglsink_get_caps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_droideglsink_set_caps);
  gstbasesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_droideglsink_propose_allocation);
  videosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_droideglsink_show_frame);
  klass->buffers_invalidated =
      GST_DEBUG_FUNCPTR (gst_droideglsink_buffers_invalidated);

  gst_droideglsink_signals[SHOW_FRAME] =
      g_signal_new ("show-frame", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDroidEglSinkClass,
          signal_show_frame), NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GST_TYPE_BUFFER);
  gst_droideglsink_signals[BUFFERS_INVALIDATED] =
      g_signal_new ("buffers-invalidated", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstDroidEglSinkClass,
          signal_buffers_invalidated), NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 0);

  g_object_class_install_property (gobject_class, PROP_EGL_DISPLAY,
      g_param_spec_pointer ("egl-display",
          "EGL display ",
          "The application provided EGL display to be used for creating EGLImageKHR objects.",
          G_PARAM_READWRITE));
}
