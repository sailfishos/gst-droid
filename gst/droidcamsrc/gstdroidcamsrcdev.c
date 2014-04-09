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
gst_droidcamsrc_dev_new (camera_module_t * hw, GstDroidCamSrcPad * vfsrc)
{
  GstDroidCamSrcDev *dev;

  GST_DEBUG ("dev new");

  dev = g_slice_new0 (GstDroidCamSrcDev);

  dev->hw = hw;
  dev->vfsrc = vfsrc;

  g_mutex_init (&dev->lock);

  return dev;
}

gboolean
gst_droidcamsrc_dev_open (GstDroidCamSrcDev * dev, const gchar * id)
{
  int err;

  GST_DEBUG ("dev open");

  g_mutex_lock (&dev->lock);

  err =
      dev->hw->common.methods->open ((const struct hw_module_t *) dev->hw, id,
      (struct hw_device_t **) &dev->dev);
  if (err < 0) {
    dev->dev = NULL;

    g_mutex_unlock (&dev->lock);

    GST_ERROR ("error 0x%x opening camera", err);
    return FALSE;
  }

  g_mutex_unlock (&dev->lock);

  return TRUE;
}

void
gst_droidcamsrc_dev_close (GstDroidCamSrcDev * dev)
{
  int err;

  GST_DEBUG ("dev close");

  g_mutex_lock (&dev->lock);

  if (dev->dev) {
    dev->dev->ops->release (dev->dev);
    err = dev->dev->common.close (&dev->dev->common);
    dev->dev = NULL;

    if (err < 0) {
      GST_ERROR ("error 0x%x closing camera", err);
    }
  }

  g_mutex_unlock (&dev->lock);
}

void
gst_droidcamsrc_dev_destroy (GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev destroy");

  dev->hw = NULL;
  g_mutex_clear (&dev->lock);
  g_slice_free (GstDroidCamSrcDev, dev);
  dev = NULL;
}

gboolean
gst_droidcamsrc_dev_init (GstDroidCamSrcDev * dev)
{
  gchar *params;
  int err;

  GST_DEBUG ("dev init");

  g_mutex_lock (&dev->lock);

  dev->win = gst_droid_cam_src_stream_window_new (dev->vfsrc);

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

  err = dev->dev->ops->set_preview_window (dev->dev, &dev->win->window);
  if (err != 0) {
    GST_ERROR ("error 0x%x setting preview window", err);
    goto err;
  }

  g_mutex_unlock (&dev->lock);
  return TRUE;

err:
  g_mutex_unlock (&dev->lock);
  gst_droidcamsrc_dev_deinit (dev);
  return FALSE;
}

void
gst_droidcamsrc_dev_deinit (GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev deinit");

  g_mutex_lock (&dev->lock);

  if (dev->params) {
    gst_droidcamsrc_params_destroy (dev->params);
    dev->params = NULL;
  }

  if (dev->win) {
    gst_droid_cam_src_stream_window_destroy (dev->win);
  }

  g_mutex_unlock (&dev->lock);
}

gboolean
gst_droidcamsrc_dev_start (GstDroidCamSrcDev * dev)
{
  int err;
  gboolean ret = FALSE;

  GST_DEBUG ("dev start");
  g_mutex_lock (&dev->lock);
  err = dev->dev->ops->start_preview (dev->dev);
  if (err != 0) {
    GST_ERROR ("error 0x%x starting preview", err);
    goto out;
  }
  // TODO: set params?

  ret = TRUE;

out:
  g_mutex_unlock (&dev->lock);
  return ret;
}

void
gst_droidcamsrc_dev_stop (GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev stop");

  g_mutex_lock (&dev->lock);

  dev->dev->ops->stop_preview (dev->dev);

  gst_droid_cam_src_stream_window_clear (dev->win);

  g_mutex_unlock (&dev->lock);
}

gboolean
gst_droidcamsrc_dev_set_params (GstDroidCamSrcDev * dev, const gchar * params)
{
  int err;
  gboolean ret = FALSE;

  g_mutex_lock (&dev->lock);
  if (!dev->dev) {
    GST_ERROR ("camera device is not open");
    goto out;
  }

  err = dev->dev->ops->set_parameters (dev->dev, params);
  if (err != 0) {
    GST_ERROR ("error 0x%x setting parameters", err);
    goto out;
  }

  ret = TRUE;

out:
  g_mutex_unlock (&dev->lock);

  return ret;
}

gboolean
gst_droidcamsrc_dev_capture_image (GstDroidCamSrcDev * dev)
{
  int err;
  gboolean ret = FALSE;
  int msg_type =
      CAMERA_MSG_SHUTTER | CAMERA_MSG_POSTVIEW_FRAME | CAMERA_MSG_RAW_IMAGE |
      CAMERA_MSG_COMPRESSED_IMAGE;

  GST_DEBUG ("dev capture image");

  g_mutex_lock (&dev->lock);

  dev->dev->ops->enable_msg_type (dev->dev, msg_type);

  err = dev->dev->ops->take_picture (dev->dev);
  if (err != 0) {
    GST_ERROR ("error 0x%x capturing image", err);
    goto out;
  }

  ret = TRUE;

out:
  g_mutex_unlock (&dev->lock);
  return ret;
}
