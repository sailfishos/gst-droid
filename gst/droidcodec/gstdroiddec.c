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

#include "gstdroiddec.h"
#include "gst/memory/gstgralloc.h"

#define gst_droiddec_parent_class parent_class
G_DEFINE_TYPE (GstDroidDec, gst_droiddec, GST_TYPE_VIDEO_DECODER);

GST_DEBUG_CATEGORY_STATIC (gst_droid_dec_debug);
#define GST_CAT_DEFAULT gst_droid_dec_debug

static GstStaticPadTemplate gst_droiddec_src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_SURFACE, "{ENCODED, YV12}")));

static GstStaticPadTemplate gst_droiddec_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
gst_droiddec_init (GstDroidDec * dec)
{
  // TODO:
}

static void
gst_droiddec_class_init (GstDroidDecClass * klass)
{
  GstElementClass *gstelement_class;
  // TODO:

  gstelement_class = (GstElementClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Video sink", "Decoder/Video/Device",
      "Android HAL decoder", "Mohammed Sameer <msameer@foolab.org>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droiddec_sink_template_factory));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droiddec_src_template_factory));

}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_droid_dec_debug, "droiddec",
      0, "Android HAL decoder");

  return gst_element_register (plugin, "droiddec", GST_RANK_PRIMARY + 1,
      GST_TYPE_DROIDDEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    droiddec,
    "Android HAL decoder",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, "http://foolab.org/")
