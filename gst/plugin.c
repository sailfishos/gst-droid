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

#include <gst/gst.h>
#include "plugin.h"
#include "gstdroidcamsrc.h"
#include "gstdroideglsink.h"
#include "gstdroiddec.h"
#include "gstdroidenc.h"

GST_DEBUG_CATEGORY (gst_droid_camsrc_debug);
GST_DEBUG_CATEGORY (gst_droid_dec_debug);
GST_DEBUG_CATEGORY (gst_droid_enc_debug);
GST_DEBUG_CATEGORY (gst_droid_codec_debug);
GST_DEBUG_CATEGORY (gst_droid_eglsink_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ok = TRUE;

  GST_DEBUG_CATEGORY_INIT (gst_droid_camsrc_debug, "droidcamsrc",
      0, "Android HAL camera source");

  GST_DEBUG_CATEGORY_INIT (gst_droid_eglsink_debug, "droideglsink",
      0, "Android EGL sink");

  GST_DEBUG_CATEGORY_INIT (gst_droid_dec_debug, "droiddec",
      0, "Android HAL decoder");

  GST_DEBUG_CATEGORY_INIT (gst_droid_enc_debug, "droidenc",
      0, "Android HAL encoder");

  GST_DEBUG_CATEGORY_INIT (gst_droid_codec_debug, "droidcodec",
      0, "Android HAL codec");

  ok &= gst_element_register (plugin, "droidcamsrc", GST_RANK_PRIMARY,
      GST_TYPE_DROIDCAMSRC);
  ok &= gst_element_register (plugin, "droideglsink", GST_RANK_PRIMARY,
      GST_TYPE_DROIDEGLSINK);

  ok &= gst_element_register (plugin, "droiddec", GST_RANK_PRIMARY + 1,
      GST_TYPE_DROIDDEC);
  ok &= gst_element_register (plugin, "droidenc", GST_RANK_PRIMARY + 1,
      GST_TYPE_DROIDENC);

  return ok;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    droid,
    "Android HAL plugins",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, "http://foolab.org/")
