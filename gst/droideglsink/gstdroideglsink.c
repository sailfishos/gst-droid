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

#include "gstdroideglsink.h"
#include <gst/video/video.h>
#include <gst/interfaces/nemovideotexture.h>

GST_DEBUG_CATEGORY_STATIC (gst_droid_egl_sink_debug);
#define GST_CAT_DEFAULT gst_droid_egl_sink_debug

static void gst_droideglsink_video_texture_init (NemoGstVideoTextureClass * iface);

#define gst_droideglsink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDroidEglSink, gst_droideglsink, GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (NEMO_GST_TYPE_VIDEO_TEXTURE,
        gst_droideglsink_video_texture_init));

static GstStaticPadTemplate gst_droideglsink_sink_template_factory =
  GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw, "
          "framerate = (fraction) [ 0, MAX ], "
	  "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ], "
	  "format = {NV12}")
      );

static GstCaps *
gst_droideglsink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstDroidEglSink *sink;
  GstCaps *caps;

  sink = GST_DROIDEGLSINK (bsink);

  GST_DEBUG_OBJECT (sink, "get caps called with filter caps %" GST_PTR_FORMAT, filter);
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
    GST_DEBUG_OBJECT (sink,
		      "Could not locate image format from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  sink->fps_n = info.fps_n;
  sink->fps_d = info.fps_d;

  GST_VIDEO_SINK_WIDTH (vsink) = info.width;
  GST_VIDEO_SINK_HEIGHT (vsink) = info.height;
  // TODO:
  return TRUE;
}

static void
gst_droideglsink_get_times (GstBaseSink * bsink, GstBuffer * buf,
			   GstClockTime * start, GstClockTime * end)
{
  GstDroidEglSink *sink;

  sink = GST_DROIDEGLSINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = *start + GST_BUFFER_DURATION (buf);
    } else {
      if (sink->fps_n > 0) {
        *end = *start +
	  gst_util_uint64_scale_int (GST_SECOND, sink->fps_d,
				     sink->fps_n);
      }
    }
  }
}

static gboolean
gst_droideglsink_start (GstBaseSink * bsink)
{
  GstDroidEglSink *sink;

  sink = GST_DROIDEGLSINK (bsink);

  GST_DEBUG_OBJECT (sink, "start");

  sink->fps_n = 0;
  sink->fps_d = 1;

  // TODO:

  return TRUE;
}

static gboolean
gst_droideglsink_stop (GstBaseSink * bsink)
{
  GstDroidEglSink *sink;

  sink = GST_DROIDEGLSINK (bsink);

  GST_DEBUG_OBJECT (sink, "stop");

  // TODO:

  return TRUE;
}

static GstFlowReturn
gst_droideglsink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstDroidEglSink *sink = GST_DROIDEGLSINK (vsink);

  GST_DEBUG_OBJECT (sink, "show frame");
  // TODO:

  return GST_FLOW_OK;
}

static gboolean
gst_droideglsink_event (GstBaseSink * sink, GstEvent * event)
{
  // TODO:

  return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
}

static void
gst_droideglsink_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_droideglsink_init (GstDroidEglSink * sink)
{
  sink->fps_n = 0;
  sink->fps_d = 0;
  // TODO:
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

  gobject_class->finalize = gst_droideglsink_finalize;

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_droideglsink_get_caps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_droideglsink_set_caps);
  gstbasesink_class->get_times = GST_DEBUG_FUNCPTR (gst_droideglsink_get_times);
  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_droideglsink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_droideglsink_stop);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_droideglsink_event);

  videosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_droideglsink_show_frame);
}

/* interfaces */
static gboolean
gst_droidcamsrc_acquire_frame (NemoGstVideoTexture * iface)
{
  // TODO:

  return TRUE;
}

static gboolean
gst_droidcamsrc_bind_frame (NemoGstVideoTexture * iface, EGLImageKHR *image)
{
  // TODO:

  return TRUE;
}

static void
gst_droidcamsrc_unbind_frame (NemoGstVideoTexture * iface)
{

}

static void
gst_droidcamsrc_release_frame (NemoGstVideoTexture * iface, EGLSyncKHR sync)
{

}

static gboolean
gst_droidcamsrc_get_frame_info (NemoGstVideoTexture * iface, NemoGstVideoTextureFrameInfo *info)
{
  // TODO:

  return TRUE;
}

static GstMeta *
gst_droidcamsrc_get_frame_meta (NemoGstVideoTexture * iface, GType api)
{
  // TODO:

  return NULL;
}

static void
gst_droideglsink_video_texture_init (NemoGstVideoTextureClass * iface)
{
  iface->acquire_frame = gst_droidcamsrc_acquire_frame;
  iface->bind_frame = gst_droidcamsrc_bind_frame;
  iface->unbind_frame = gst_droidcamsrc_unbind_frame;
  iface->release_frame = gst_droidcamsrc_release_frame;
  iface->get_frame_info = gst_droidcamsrc_get_frame_info;
  iface->get_frame_meta = gst_droidcamsrc_get_frame_meta;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_droid_egl_sink_debug, "droideglsink",
      0, "Android HAL plugin");

  return gst_element_register (plugin, "droideglsink", GST_RANK_PRIMARY,
			       GST_TYPE_DROIDEGLSINK);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    droideglsink,
    "Android EGL sink",
    plugin_init,
    VERSION,
    "LGPL",
    PACKAGE_NAME,
    "http://foolab.org/"
)
