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
#include "gstdroidcamsrcdevmemory.h"
#include <stdlib.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

static camera_memory_t *
gst_droidcamsrc_dev_request_memory (int fd, size_t buf_size,
    unsigned int num_bufs, void *user)
{
  GST_DEBUG ("dev request memory fd=%d, buf_size=%d, num_bufs=%d", fd, buf_size,
      num_bufs);

  return gst_droidcamsrc_dev_memory_get (fd, buf_size, num_bufs);
}

static void
gst_droidcamsrc_dev_notify_callback (int32_t msg_type,
    int32_t ext1, int32_t ext2, void *user)
{
  // TODO:
}

static void
gst_droidcamsrc_dev_data_callback (int32_t msg_type,
    const camera_memory_t * data, unsigned int index,
    camera_frame_metadata_t * metadata, void *user)
{
  // TODO:
}

static void
gst_droidcamsrc_dev_data_timestamp_callback (int64_t timestamp,
    int32_t msg_type, const camera_memory_t * data,
    unsigned int index, void *user)
{
  // TODO:
}

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

gboolean
gst_droidcamsrc_dev_init (GstDroidCamSrcDev * dev)
{
  gchar *params;
  int err;

  GST_DEBUG ("dev init");

  dev->pool = gst_droid_cam_src_buffer_pool_new ();

  params = dev->dev->ops->get_parameters (dev->dev);

  dev->params = gst_droidcamsrc_params_new (params);

  dev->dev->ops->set_parameters (dev->dev, params);

  if (dev->dev->ops->put_parameters) {
    dev->dev->ops->put_parameters (dev->dev, params);
  } else {
    free (params);
  }

  params = NULL;

  dev->dev->ops->set_callbacks (dev->dev, gst_droidcamsrc_dev_notify_callback,
      gst_droidcamsrc_dev_data_callback,
      gst_droidcamsrc_dev_data_timestamp_callback,
      gst_droidcamsrc_dev_request_memory, dev);

  err = dev->dev->ops->set_preview_window (dev->dev, &dev->pool->window);
  if (err != 0) {
    GST_ERROR ("error 0x%x setting preview window", err);
    goto err;
  }

  return TRUE;

err:
  gst_droidcamsrc_dev_deinit (dev);
  return FALSE;
}

void
gst_droidcamsrc_dev_deinit (GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev deinit");

  if (dev->pool) {
    gst_object_unref (GST_OBJECT (dev->pool));
    dev->pool = NULL;
  }

  if (dev->params) {
    gst_droidcamsrc_params_destroy (dev->params);
    dev->params = NULL;
  }
}

gboolean
gst_droidcamsrc_dev_start (GstDroidCamSrcDev * dev)
{
  int err;

  GST_DEBUG ("dev start");

  err = dev->dev->ops->start_preview (dev->dev);
  if (err != 0) {
    GST_ERROR ("error 0x%x starting preview", err);
    goto error;
  }
  // TODO: set params?

  return TRUE;

error:
  gst_droid_cam_src_buffer_pool_reset (dev->pool);
  return FALSE;
}

void
gst_droidcamsrc_dev_stop (GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev stop");

  dev->dev->ops->stop_preview (dev->dev);

  if (!gst_buffer_pool_set_active (GST_BUFFER_POOL (dev->pool), FALSE)) {
    GST_ERROR ("failed to deactivate buffer pool");
  }

  gst_droid_cam_src_buffer_pool_reset (dev->pool);
}
