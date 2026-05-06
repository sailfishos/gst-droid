/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer
 * Copyright (C) 2020-2021 Jolla Ltd.
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

#include <gst/gst.h>
#include "plugin.h"
#include "gstdroidcamsrc.h"
#include "gstdroideglsink.h"
#include "gstdroidvideotexturesink.h"
#include "gstdroidvdec.h"
#include "gstdroidvenc.h"
#include "gstdroidadec.h"
#include "gstdroidaenc.h"
#include "droidmedia.h"

GST_DEBUG_CATEGORY (gst_droid_camsrc_debug);
GST_DEBUG_CATEGORY (gst_droid_adec_debug);
GST_DEBUG_CATEGORY (gst_droid_aenc_debug);
GST_DEBUG_CATEGORY (gst_droid_vdec_debug);
GST_DEBUG_CATEGORY (gst_droid_venc_debug);
GST_DEBUG_CATEGORY (gst_droid_codec_debug);
GST_DEBUG_CATEGORY (gst_droid_eglsink_debug);
GST_DEBUG_CATEGORY (gst_droid_videotexturesink_debug);

#define CAMERA_STARTUP_PLUGIN(format, ...) \
  G_STMT_START { \
    if (gst_droid_camera_startup_logging_enabled ()) \
      g_message ("CAMERA_STARTUP gst-droid-plugin %" G_GINT64_FORMAT " ms " format, \
          gst_droid_camera_startup_mono_ms (), ##__VA_ARGS__); \
  } G_STMT_END

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ok = TRUE;

  CAMERA_STARTUP_PLUGIN ("plugin_init begin");

  GST_DEBUG_CATEGORY_INIT (gst_droid_camsrc_debug, "droidcamsrc",
      0, "Android HAL camera source");

  GST_DEBUG_CATEGORY_INIT (gst_droid_eglsink_debug, "droideglsink",
      0, "Android EGL sink");

  GST_DEBUG_CATEGORY_INIT (gst_droid_videotexturesink_debug,
      "droidvideotexturesink", 0, "Android EGL sink");

  GST_DEBUG_CATEGORY_INIT (gst_droid_adec_debug, "droidadec",
      0, "Android HAL audio decoder");

  GST_DEBUG_CATEGORY_INIT (gst_droid_aenc_debug, "droidaenc",
      0, "Android HAL audio encoder");

  GST_DEBUG_CATEGORY_INIT (gst_droid_vdec_debug, "droidvdec",
      0, "Android HAL video decoder");

  GST_DEBUG_CATEGORY_INIT (gst_droid_venc_debug, "droidvenc",
      0, "Android HAL video encoder");

  GST_DEBUG_CATEGORY_INIT (gst_droid_codec_debug, "droidcodec",
      0, "Android HAL codec");

  CAMERA_STARTUP_PLUGIN ("debug categories done");

  ok &= gst_element_register (plugin, "droidcamsrc", GST_RANK_PRIMARY,
      GST_TYPE_DROIDCAMSRC);
  CAMERA_STARTUP_PLUGIN ("registered droidcamsrc ok=%d", ok);
  ok &= gst_element_register (plugin, "droideglsink", GST_RANK_PRIMARY,
      GST_TYPE_DROIDEGLSINK);
  ok &= gst_element_register (plugin, "droidvideotexturesink", GST_RANK_PRIMARY,
      GST_TYPE_DROIDVIDEOTEXTURESINK);

  CAMERA_STARTUP_PLUGIN ("registered sinks ok=%d", ok);

  ok &= gst_element_register (plugin, "droidvdec", GST_RANK_PRIMARY + 1,
      GST_TYPE_DROIDVDEC);
  ok &= gst_element_register (plugin, "droidvenc", GST_RANK_PRIMARY + 1,
      GST_TYPE_DROIDVENC);
  ok &= gst_element_register (plugin, "droidadec", GST_RANK_PRIMARY + 1,
      GST_TYPE_DROIDADEC);
  ok &= gst_element_register (plugin, "droidaenc", GST_RANK_PRIMARY + 1,
      GST_TYPE_DROIDAENC);

  CAMERA_STARTUP_PLUGIN ("registered codecs ok=%d", ok);

  if (ok) {
    CAMERA_STARTUP_PLUGIN ("droid_media_init begin");
    ok = droid_media_init ();
    CAMERA_STARTUP_PLUGIN ("droid_media_init done ok=%d", ok);
  }

  CAMERA_STARTUP_PLUGIN ("plugin_init done ok=%d", ok);

  return ok;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    droid,
    "Android HAL plugins",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, "http://sailfishos.org/")
