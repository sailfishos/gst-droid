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

#include "gstdroidcamsrcdev.h"

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

GstDroidCamSrcDev *
gst_droidcamsrc_dev_new (camera_module_t * hw)
{
  GstDroidCamSrcDev *dev;

  GST_DEBUG ("dev new");

  dev = g_slice_new0 (GstDroidCamSrcDev);

  dev->hw = hw;

  return dev;
}

gboolean
gst_droidcamsrc_dev_open (GstDroidCamSrcDev * dev, const gchar * id)
{
  int err;

  GST_DEBUG ("dev open");

  err =
      dev->hw->common.methods->open ((const struct hw_module_t *) dev->hw, "0",
      (struct hw_device_t **) &dev->dev);
  if (err < 0) {
    dev->dev = NULL;
    GST_ERROR ("error 0x%x opening camera", err);
    return FALSE;
  }

  return TRUE;
}

void
gst_droidcamsrc_dev_close (GstDroidCamSrcDev * dev)
{
  int err;

  GST_DEBUG ("dev close");

  if (dev->dev) {
    dev->dev->ops->release (dev->dev);
    err = dev->dev->common.close (&dev->dev->common);
    dev->dev = NULL;

    if (err < 0) {
      GST_ERROR ("error 0x%x closing camera", err);
    }
  }
}

void
gst_droidcamsrc_dev_destroy (GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev destroy");

  dev->hw = NULL;
  g_slice_free (GstDroidCamSrcDev, dev);
  dev = NULL;
}
