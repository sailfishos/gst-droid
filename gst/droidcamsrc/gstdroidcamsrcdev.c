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
#include "gstdroidcamsrc.h"
#include <gst/memory/gstwrappedmemory.h>
#include <unistd.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

#define VIDEO_RECORDING_STOP_TIMEOUT                 100000     /* us */

struct _GstDroidCamSrcImageCaptureState
{
  gboolean image_preview_sent;
};

struct _GstDroidCamSrcVideoCaptureState
{
  unsigned long video_frames;
  int queued_frames;
  gboolean running;
  gboolean eos_sent;
  GMutex lock;
};

static void gst_droidcamsrc_dev_release_recording_frame (void *data,
    GstDroidCamSrcDev * dev);

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
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  GST_DEBUG ("dev notify callback");

  // TODO: more messages

  switch (msg_type) {
    case CAMERA_MSG_SHUTTER:
      g_mutex_lock (&dev->lock);
      dev->dev->ops->disable_msg_type (dev->dev, CAMERA_MSG_SHUTTER);
      gst_droidcamsrc_post_message (src,
          gst_structure_new_empty (GST_DROIDCAMSRC_CAPTURE_START));
      g_mutex_unlock (&dev->lock);
      break;

    default:
      GST_WARNING ("unknown message type 0x%x", msg_type);
  }
}

static void
gst_droidcamsrc_dev_data_callback (int32_t msg_type,
    const camera_memory_t * data, unsigned int index,
    camera_frame_metadata_t * metadata, void *user)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  GST_DEBUG ("dev data callback");

  switch (msg_type) {
    case CAMERA_MSG_RAW_IMAGE:
      // TODO:
      break;

    case CAMERA_MSG_COMPRESSED_IMAGE:
    {
      size_t size;
      void *addr = gst_droidcamsrc_dev_memory_get_data (data, index, &size);
      if (!addr) {
        GST_ERROR_OBJECT (src, "invalid memory from camera hal");
      } else {
        GstBuffer *buffer;
        void *d = g_malloc (size);
        memcpy (d, addr, size);
        buffer = gst_buffer_new_wrapped (d, size);
        if (!dev->img->image_preview_sent) {
          gst_droidcamsrc_post_message (src,
              gst_structure_new_empty (GST_DROIDCAMSRC_CAPTURE_END));
          // TODO: generate and send preview.
          dev->img->image_preview_sent = TRUE;
        }

        gst_droidcamsrc_timestamp (src, buffer);

        // TODO: extract and post exif
        g_mutex_lock (&dev->imgsrc->lock);
        g_queue_push_tail (dev->imgsrc->queue, buffer);
        g_cond_signal (&dev->imgsrc->cond);
        g_mutex_unlock (&dev->imgsrc->lock);
      }

      gst_droidcamsrc_dev_start (dev);

      g_mutex_lock (&src->capture_lock);
      --src->captures;
      g_mutex_unlock (&src->capture_lock);

      g_object_notify (G_OBJECT (src), "ready-for-capture");
    }
      break;

    default:
      GST_WARNING ("unknown message type 0x%x", msg_type);
  }

  // TODO:
}

static void
gst_droidcamsrc_dev_data_timestamp_callback (int64_t timestamp,
    int32_t msg_type, const camera_memory_t * data,
    unsigned int index, void *user)
{
  void *addr;
  size_t size;
  gboolean drop_buffer;
  GstBuffer *buffer;
  GstMemory *mem;
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  g_mutex_lock (&dev->vid->lock);

  // TODO: not sure what to do with timestamp

  GST_DEBUG ("dev data timestamp callback");

  /* unlikely but just in case */
  if (msg_type != CAMERA_MSG_VIDEO_FRAME) {
    GST_ERROR ("unknown message type 0x%x", msg_type);
    goto unlock_and_out;
  }

  addr = gst_droidcamsrc_dev_memory_get_data (data, index, &size);
  if (!addr) {
    GST_ERROR ("invalid memory from camera HAL");
    goto unlock_and_out;
  }

  buffer = gst_buffer_new ();
  mem = gst_wrapped_memory_allocator_wrap (dev->allocator,
      addr, size, (GFunc) gst_droidcamsrc_dev_release_recording_frame, dev);
  gst_buffer_insert_memory (buffer, 0, mem);

  GST_BUFFER_OFFSET (buffer) = dev->vid->video_frames;
  GST_BUFFER_OFFSET_END (buffer) = ++dev->vid->video_frames;
  gst_droidcamsrc_timestamp (src, buffer);

  g_mutex_lock (&dev->lock);

  drop_buffer = !dev->vid->running;
  if (!drop_buffer) {
    ++dev->vid->queued_frames;
  }

  g_mutex_unlock (&dev->lock);

  if (drop_buffer) {
    gst_buffer_unref (buffer);
  } else {
    g_mutex_lock (&dev->vidsrc->lock);
    g_queue_push_tail (dev->vidsrc->queue, buffer);
    g_cond_signal (&dev->vidsrc->cond);
    g_mutex_unlock (&dev->vidsrc->lock);
  }

unlock_and_out:
  g_mutex_unlock (&dev->vid->lock);
}

GstDroidCamSrcDev *
gst_droidcamsrc_dev_new (camera_module_t * hw, GstDroidCamSrcPad * vfsrc,
    GstDroidCamSrcPad * imgsrc, GstDroidCamSrcPad * vidsrc)
{
  GstDroidCamSrcDev *dev;

  GST_DEBUG ("dev new");

  dev = g_slice_new0 (GstDroidCamSrcDev);

  dev->info = NULL;
  dev->img = g_slice_new0 (GstDroidCamSrcImageCaptureState);
  dev->vid = g_slice_new0 (GstDroidCamSrcVideoCaptureState);
  g_mutex_init (&dev->vid->lock);

  dev->allocator = gst_wrapped_memory_allocator_new ();
  dev->hw = hw;
  dev->vfsrc = vfsrc;
  dev->imgsrc = imgsrc;
  dev->vidsrc = vidsrc;

  g_mutex_init (&dev->lock);

  return dev;
}

gboolean
gst_droidcamsrc_dev_open (GstDroidCamSrcDev * dev, GstDroidCamSrcCamInfo * info)
{
  int err;
  gchar *id;

  GST_DEBUG ("dev open");

  g_mutex_lock (&dev->lock);

  dev->info = info;
  id = g_strdup_printf ("%d", dev->info->num);

  err =
      dev->hw->common.methods->open ((const struct hw_module_t *) dev->hw, id,
      (struct hw_device_t **) &dev->dev);

  g_free (id);

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
  dev->info = NULL;
  gst_object_unref (dev->allocator);
  g_mutex_clear (&dev->lock);
  g_mutex_init (&dev->vid->lock);
  g_slice_free (GstDroidCamSrcImageCaptureState, dev->img);
  g_slice_free (GstDroidCamSrcVideoCaptureState, dev->vid);
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

  dev->win = gst_droid_cam_src_stream_window_new (dev->vfsrc, dev->info);

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
  dev->img->image_preview_sent = FALSE;

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

gboolean
gst_droidcamsrc_dev_start_video_recording (GstDroidCamSrcDev * dev)
{
  int err;
  gboolean ret = FALSE;
  int msg_type = CAMERA_MSG_VIDEO_FRAME;

  GST_DEBUG ("dev start video recording");

  g_mutex_lock (&dev->vidsrc->lock);
  dev->vidsrc->pushed_buffers = 0;
  g_mutex_unlock (&dev->vidsrc->lock);

  g_mutex_lock (&dev->lock);
  dev->vid->running = TRUE;
  dev->vid->eos_sent = FALSE;
  dev->vid->video_frames = 0;
  dev->vid->queued_frames = 0;
  dev->dev->ops->enable_msg_type (dev->dev, msg_type);

  // TODO: get that from caps
  err = dev->dev->ops->store_meta_data_in_buffers (dev->dev, 1);
  if (err != 0) {
    GST_ERROR ("error 0x%x storing meta data in buffers for video recording",
        err);
    goto out;
  }

  err = dev->dev->ops->start_recording (dev->dev);
  if (err != 0) {
    GST_ERROR ("error 0x%x starting video recording", err);
    goto out;
  }

  ret = TRUE;

out:
  g_mutex_unlock (&dev->lock);
  return ret;
}

void
gst_droidcamsrc_dev_stop_video_recording (GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev stop video recording");

  // TODO: review all those locks
  /* We need to make sure that some buffers have been pushed */
  g_mutex_lock (&dev->vidsrc->lock);
  while (dev->vid->video_frames <= 4) {
    g_mutex_unlock (&dev->vidsrc->lock);
    usleep (30000);             // TODO: bad
    g_mutex_lock (&dev->vidsrc->lock);
  }

  g_mutex_unlock (&dev->vidsrc->lock);

  /* Now stop pushing to the pad */
  g_mutex_lock (&dev->lock);
  dev->vid->running = FALSE;
  g_mutex_unlock (&dev->lock);

  /* now make sure nothing is being pushed to the queue */
  g_mutex_lock (&dev->vid->lock);
  g_mutex_unlock (&dev->vid->lock);

  /* our pad task is either sleeping or still pushing buffers. We empty the queue. */
  g_mutex_lock (&dev->vidsrc->lock);
  g_queue_foreach (dev->vidsrc->queue, (GFunc) gst_buffer_unref, NULL);
  g_mutex_unlock (&dev->vidsrc->lock);

  /* now we are done. We just push eos */
  if (!gst_pad_push_event (dev->vidsrc->pad, gst_event_new_eos ())) {
    GST_ERROR ("failed to push EOS event");
    dev->dev->ops->stop_recording (dev->dev);
    return;
  }

  g_mutex_lock (&dev->lock);

  GST_INFO ("waiting for queued frames %i", dev->vid->queued_frames);

  if (dev->vid->queued_frames > 0) {
    GST_INFO ("waiting for queued frames to reach 0 from %i",
        dev->vid->queued_frames);
    g_mutex_unlock (&dev->lock);
    usleep (VIDEO_RECORDING_STOP_TIMEOUT);
    g_mutex_lock (&dev->lock);
  }

  if (dev->vid->queued_frames > 0) {
    GST_WARNING ("video queue still has %i frames", dev->vid->queued_frames);
  }

  g_mutex_unlock (&dev->lock);

  dev->dev->ops->stop_recording (dev->dev);
}

static void
gst_droidcamsrc_dev_release_recording_frame (void *data,
    GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev release recording frame %p", data);

  g_mutex_lock (&dev->lock);
  --dev->vid->queued_frames;
  dev->dev->ops->release_recording_frame (dev->dev, data);
  g_mutex_unlock (&dev->lock);
}
