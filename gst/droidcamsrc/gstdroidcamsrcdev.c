/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
 * Copyright (C) 2015-2016 Jolla LTD.
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

#include "gstdroidcamsrcdev.h"
#include <stdlib.h>
#include "gstdroidcamsrc.h"
#include "gstdroidcamsrcrecorder.h"
#include "gst/droid/gstdroidmediabuffer.h"
#include "gst/droid/gstwrappedmemory.h"
#include "gst/droid/gstdroidbufferpool.h"
#include <unistd.h>             /* usleep() */
#include <string.h>             /* memcpy() */
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include <gst/interfaces/photography.h>
#include "gstdroidcamsrcexif.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droid_camsrc_debug);
#define GST_CAT_DEFAULT gst_droid_camsrc_debug

#define VIDEO_RECORDING_STOP_TIMEOUT                 100000     /* us */
#define GST_DROIDCAMSRC_NUM_BUFFERS                  2

struct _GstDroidCamSrcImageCaptureState
{
  gboolean image_preview_sent;
  gboolean image_start_sent;
};

struct _GstDroidCamSrcVideoCaptureState
{
  unsigned long video_frames;
  int queued_frames;
  gboolean running;
  gboolean eos_sent;
  GMutex lock;
  GCond cond;
};

typedef struct _GstDroidCamSrcDevVideoData
{
  GstDroidCamSrcDev *dev;
  DroidMediaCameraRecordingData *data;
} GstDroidCamSrcDevVideoData;

static void gst_droidcamsrc_dev_release_recording_frame (void *data,
    GstDroidCamSrcDevVideoData * video_data);
void gst_droidcamsrc_dev_update_params_locked (GstDroidCamSrcDev * dev);
static void
gst_droidcamsrc_dev_prepare_buffer (GstDroidCamSrcDev * dev, GstBuffer * buffer,
    DroidMediaRect rect, int width, int height, GstVideoFormat format);
static gboolean
gst_droidcamsrc_dev_start_video_recording_recorder_locked (GstDroidCamSrcDev *
    dev);
static gboolean
gst_droidcamsrc_dev_start_video_recording_raw_locked (GstDroidCamSrcDev * dev);
static void gst_droidcamsrc_dev_queue_video_buffer_locked (GstDroidCamSrcDev *
    dev, GstBuffer * buffer);

static void
gst_droidcamsrc_dev_shutter_callback (void *user)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  GST_DEBUG_OBJECT (src, "dev shutter callback");

  g_rec_mutex_lock (dev->lock);

  if (!dev->img->image_start_sent) {
    gst_droidcamsrc_post_message (src,
        gst_structure_new_empty (GST_DROIDCAMSRC_CAPTURE_START));
    dev->img->image_start_sent = TRUE;
  }

  g_rec_mutex_unlock (dev->lock);
}

static void
gst_droidcamsrc_dev_focus_callback (void *user, int arg)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));
  GstStructure *s;
  gint status;

  GST_DEBUG_OBJECT (src, "dev focus callback");

  if (arg) {
    status = GST_PHOTOGRAPHY_FOCUS_STATUS_SUCCESS;
  } else {
    status = GST_PHOTOGRAPHY_FOCUS_STATUS_FAIL;
  }

  s = gst_structure_new (GST_PHOTOGRAPHY_AUTOFOCUS_DONE, "status",
      G_TYPE_INT, status, NULL);
  gst_droidcamsrc_post_message (src, s);
}

static void
gst_droidcamsrc_dev_focus_move_callback (void *user, int arg)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));
  GstStructure *s;

  GST_DEBUG_OBJECT (src, "dev focus move callback");

  /* TODO: an idea could be to query focus state when moving stops or starts
   * and use that to emulate realtime reporting of CAF status */

  GST_LOG_OBJECT (src, "focus move %d", arg);

  s = gst_structure_new ("focus-move", "status", G_TYPE_INT, arg, NULL);
  gst_droidcamsrc_post_message (src, s);
}

static void
gst_droidcamsrc_dev_error_callback (void *user, int arg)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  GST_DEBUG_OBJECT (src, "dev error callback");

  GST_ELEMENT_ERROR (src, LIBRARY, FAILED, (NULL),
      ("error 0x%x from camera HAL", arg));
}

static void
gst_droidcamsrc_dev_zoom_callback (void *user, G_GNUC_UNUSED int value,
    G_GNUC_UNUSED int arg)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  GST_DEBUG_OBJECT (src, "dev zoom callback");

  GST_FIXME_OBJECT (src, "implement me");
}

static void
gst_droidcamsrc_dev_raw_image_callback (void *user,
    G_GNUC_UNUSED DroidMediaData * mem)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  GST_DEBUG_OBJECT (src, "dev raw image callback");

  GST_FIXME_OBJECT (src, "implement me");
}

static void
gst_droidcamsrc_dev_compressed_image_callback (void *user, DroidMediaData * mem)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));
  size_t size = mem->size;
  void *data = mem->data;
  GstBuffer *buffer;
  GstTagList *tags;
  GstEvent *event = NULL;
  void *d;

  GST_DEBUG_OBJECT (src, "dev compressed image callback");

  if (!data) {
    GST_ERROR_OBJECT (src, "invalid memory from camera hal");
    return;
  }

  /* TODO: research a way to get rid of the memcpy */
  d = g_malloc (size);
  memcpy (d, data, size);
  buffer = gst_buffer_new_wrapped (d, size);
  if (!dev->img->image_preview_sent) {
    gst_droidcamsrc_post_message (src,
        gst_structure_new_empty (GST_DROIDCAMSRC_CAPTURE_END));
    /* TODO: generate and send preview if we don't get it from HAL */
    dev->img->image_preview_sent = TRUE;
  }

  gst_droidcamsrc_timestamp (src, buffer);

  tags = gst_droidcamsrc_exif_tags_from_jpeg_data (d, size);
  if (tags) {
    GST_INFO_OBJECT (src, "pushing tags %" GST_PTR_FORMAT, tags);
    event = gst_event_new_tag (tags);
  }

  g_mutex_lock (&dev->imgsrc->lock);

  // TODO: get the correct lock
  if (event) {
    src->imgsrc->pending_events =
        g_list_append (src->imgsrc->pending_events, event);
  }

  g_queue_push_tail (dev->imgsrc->queue, buffer);
  g_cond_signal (&dev->imgsrc->cond);
  g_mutex_unlock (&dev->imgsrc->lock);

  /* we need to restart the preview but only if we are not in ZSL mode.
   * android demands this but GStreamer does not know about it.
   */
  if (!(src->image_mode & GST_DROIDCAMSRC_IMAGE_MODE_ZSL)) {
    g_rec_mutex_lock (dev->lock);
    dev->running = FALSE;
    g_rec_mutex_unlock (dev->lock);
    gst_droidcamsrc_dev_start (dev, TRUE);
  }

  g_mutex_lock (&src->capture_lock);
  --src->captures;
  g_mutex_unlock (&src->capture_lock);

  g_object_notify (G_OBJECT (src), "ready-for-capture");
}

static void
gst_droidcamsrc_dev_postview_frame_callback (void *user,
    G_GNUC_UNUSED DroidMediaData * mem)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  GST_DEBUG_OBJECT (src, "dev postview frame callback");

  GST_FIXME_OBJECT (src, "implement me");
}

static void
gst_droidcamsrc_dev_raw_image_notify_callback (void *user)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  GST_DEBUG_OBJECT (src, "dev raw image notify callback");

  GST_FIXME_OBJECT (src, "implement me");
}

static void
gst_droidcamsrc_dev_preview_frame_callback (void *user,
    G_GNUC_UNUSED DroidMediaData * mem)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));
  GstDroidCamSrcPad *pad = dev->vfsrc;
  GstBuffer *buffer;
  gsize width, height;
  DroidMediaRect rect;

  GST_DEBUG_OBJECT (src, "dev preview frame callback");

  /* We are accessing this without a lock because:
   * 1) We should not be called while preview is stopped and this is when we manipulate this flag
   * 2) We can get called when we start the preview and we will deadlock because the lock is already held
   */
  if (!dev->use_raw_data) {
    GST_WARNING_OBJECT (src,
        "preview frame callback called while not when we do not expect it");
    return;
  }

  buffer = gst_buffer_new_allocate (NULL, mem->size, NULL);
  gst_buffer_fill (buffer, 0, mem->data, mem->size);

  GST_OBJECT_LOCK (src);
  width = src->width;
  height = src->height;
  rect = src->crop_rect;
  GST_OBJECT_UNLOCK (src);

  gst_droidcamsrc_dev_prepare_buffer (dev, buffer, rect, width, height,
      GST_VIDEO_FORMAT_NV21);

  g_mutex_lock (&pad->lock);
  g_queue_push_tail (pad->queue, buffer);
  g_cond_signal (&pad->cond);
  g_mutex_unlock (&pad->lock);
}

static void
gst_droidcamsrc_dev_video_frame_callback (void *user,
    DroidMediaCameraRecordingData * video_data)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));
  void *data = droid_media_camera_recording_frame_get_data (video_data);
  GstBuffer *buffer;
  GstMemory *mem;
  GstDroidCamSrcDevVideoData *mem_data;

  GST_DEBUG_OBJECT (src, "dev video frame callback");

  g_mutex_lock (&dev->vid->lock);

  /* TODO: not sure what to do with timestamp */

  /* unlikely but just in case */
  if (G_UNLIKELY (!data)) {
    GST_ERROR ("invalid memory from camera HAL");
    droid_media_camera_release_recording_frame (dev->cam, video_data);
    goto unlock_and_out;
  }

  /* TODO: this is bad */
  mem_data = g_slice_new0 (GstDroidCamSrcDevVideoData);
  mem_data->dev = dev;
  mem_data->data = video_data;

  buffer = gst_buffer_new ();
  mem = gst_wrapped_memory_allocator_wrap (dev->wrap_allocator,
      data, droid_media_camera_recording_frame_get_size (video_data),
      (GFunc) gst_droidcamsrc_dev_release_recording_frame, mem_data);
  gst_buffer_insert_memory (buffer, 0, mem);

  gst_droidcamsrc_timestamp (src, buffer);

  gst_droidcamsrc_dev_queue_video_buffer_locked (dev, buffer);

  g_mutex_unlock (&dev->vid->lock);
  return;

unlock_and_out:
  /* in case stop_video_recording() is waiting for us */
  g_cond_signal (&dev->vid->cond);
  g_mutex_unlock (&dev->vid->lock);
}

static void
gst_droidcamsrc_dev_preview_metadata_callback (void *user,
    const DroidMediaCameraFace * faces, size_t num_faces)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));
  GstStructure *s;
  gint width, height;
  GValue regions = G_VALUE_INIT;
  gint i;

  GST_DEBUG_OBJECT (src, "dev preview metadata callback");

  GST_INFO_OBJECT (src, "camera detected %d faces", num_faces);

  GST_OBJECT_LOCK (src);
  width = src->width;
  height = src->height;
  GST_OBJECT_UNLOCK (src);

  s = gst_structure_new ("regions-of-interest", "frame-width", G_TYPE_UINT,
      width, "frame-height", G_TYPE_UINT, height, "type", G_TYPE_UINT,
      GST_DROIDCAMSRC_ROI_FACE_AREA, NULL);

  g_value_init (&regions, GST_TYPE_LIST);

  for (i = 0; i < num_faces; i++) {
    GValue region = G_VALUE_INIT;
    int x, y, w, h, r, b;
    GstStructure *rs;

    g_value_init (&region, GST_TYPE_STRUCTURE);

    GST_DEBUG_OBJECT (src,
        "face %d: score=%d, left=%d, top=%d, right=%d, bottom=%d", i,
        faces[i].score, faces[i].left, faces[i].top, faces[i].right,
        faces[i].bottom);
    x = gst_util_uint64_scale (faces[i].left + 1000, width, 2000);
    y = gst_util_uint64_scale (faces[i].top + 1000, height, 2000);
    r = gst_util_uint64_scale (faces[i].right + 1000, width, 2000);
    b = gst_util_uint64_scale (faces[i].bottom + 1000, height, 2000);
    w = r - x;
    h = b - y;
    rs = gst_structure_new ("region-of-interest",
        "region-x", G_TYPE_UINT, x,
        "region-y", G_TYPE_UINT, y,
        "region-w", G_TYPE_UINT, w,
        "region-h", G_TYPE_UINT, h,
        "region-id", G_TYPE_INT, faces[i].id,
        "region-score", G_TYPE_INT, faces[i].score, NULL);

    gst_value_set_structure (&region, rs);
    gst_structure_free (rs);
    gst_value_list_append_value (&regions, &region);
    g_value_unset (&region);
  }

  gst_structure_take_value (s, "regions", &regions);
  gst_droidcamsrc_post_message (src, s);
}

static void
gst_droidcamsrc_dev_buffers_released (G_GNUC_UNUSED void *user)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;

  GST_FIXME_OBJECT (dev, "Not sure what to do here really");
}

static void
gst_droidcamsrc_dev_frame_available (void *user)
{
  GstDroidCamSrcDev *dev = (GstDroidCamSrcDev *) user;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));
  GstDroidCamSrcPad *pad = dev->vfsrc;
  DroidMediaBuffer *buffer;
  GstMemory *mem;
  DroidMediaRect rect;
  guint width, height;
  GstBuffer *buff;
  DroidMediaBufferCallbacks cb;
  GstFlowReturn flow_ret;
  DroidMediaBufferInfo info;

  GST_DEBUG_OBJECT (src, "frame available");

  if (!pad->running) {
    GST_DEBUG_OBJECT (src, "vfsrc pad task is not running");

    goto acquire_and_release;
  }

  /* We are accessing this without a lock because:
   * 1) We should not be called while preview is stopped and this is when we manipulate this flag
   * 2) We can get called when we start the preview and we will deadlock because the lock is already held
   */
  if (dev->use_raw_data) {
    goto acquire_and_release;
  }

  flow_ret = gst_buffer_pool_acquire_buffer (dev->pool, &buff, NULL);
  if (flow_ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (src, "failed to acquire buffer from pool: %s",
        gst_flow_get_name (flow_ret));

    goto acquire_and_release;
  }

  cb.ref = (DroidMediaCallback) gst_buffer_ref;
  cb.unref = (DroidMediaCallback) gst_buffer_unref;
  cb.data = buff;

  mem =
      gst_droid_media_buffer_allocator_alloc (dev->media_allocator, dev->queue,
      &cb);
  if (!mem) {
    GST_ERROR_OBJECT (src, "failed to acquire buffer from droidmedia");
    gst_buffer_unref (buff);
    return;
  }

  buffer = gst_droid_media_buffer_memory_get_buffer (mem);

  gst_buffer_insert_memory (buff, 0, mem);
  gst_droidcamsrc_timestamp (src, buff);

  rect = droid_media_buffer_get_crop_rect (buffer);
  width = droid_media_buffer_get_width (buffer);
  height = droid_media_buffer_get_height (buffer);

  gst_droidcamsrc_dev_prepare_buffer (dev, buff, rect, width, height,
      GST_VIDEO_FORMAT_YV12);

  g_mutex_lock (&pad->lock);
  g_queue_push_tail (pad->queue, buff);
  g_cond_signal (&pad->cond);
  g_mutex_unlock (&pad->lock);

  GST_OBJECT_LOCK (src);
  src->crop_rect = rect;
  GST_OBJECT_UNLOCK (src);
  return;

acquire_and_release:
  if (droid_media_buffer_queue_acquire_and_release (dev->queue, &info)) {
    GST_OBJECT_LOCK (src);
    src->crop_rect = info.crop_rect;
    GST_OBJECT_UNLOCK (src);
  }
}

GstDroidCamSrcDev *
gst_droidcamsrc_dev_new (GstDroidCamSrcPad * vfsrc,
    GstDroidCamSrcPad * imgsrc, GstDroidCamSrcPad * vidsrc, GRecMutex * lock)
{
  GstDroidCamSrcDev *dev;

  GST_DEBUG ("dev new");

  dev = g_slice_new0 (GstDroidCamSrcDev);
  dev->cam = NULL;
  dev->queue = NULL;
  dev->running = FALSE;
  dev->use_raw_data = FALSE;
  dev->info = NULL;
  dev->img = g_slice_new0 (GstDroidCamSrcImageCaptureState);
  dev->vid = g_slice_new0 (GstDroidCamSrcVideoCaptureState);

  g_mutex_init (&dev->vid->lock);
  g_cond_init (&dev->vid->cond);

  dev->wrap_allocator = gst_wrapped_memory_allocator_new ();
  dev->media_allocator = gst_droid_media_buffer_allocator_new ();
  dev->vfsrc = vfsrc;
  dev->imgsrc = imgsrc;
  dev->vidsrc = vidsrc;

  dev->lock = lock;

  dev->pool = gst_droid_buffer_pool_new ();

  dev->use_recorder = FALSE;
  dev->recorder = gst_droidcamsrc_recorder_create (vidsrc);

  droid_media_camera_constants_init (&dev->c);

  return dev;
}

gboolean
gst_droidcamsrc_dev_open (GstDroidCamSrcDev * dev, GstDroidCamSrcCamInfo * info)
{
  GstDroidCamSrc *src;

  g_rec_mutex_lock (dev->lock);

  src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  GST_DEBUG_OBJECT (src, "dev open");

  dev->info = info;
  dev->cam = droid_media_camera_connect (dev->info->num);

  if (!dev->cam) {
    g_rec_mutex_unlock (dev->lock);

    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL), ("error opening camera"));
    return FALSE;
  }

  dev->queue = droid_media_camera_get_buffer_queue (dev->cam);

  if (!droid_media_camera_lock (dev->cam)) {
    droid_media_camera_disconnect (dev->cam);
    dev->cam = NULL;
    dev->queue = NULL;

    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL), ("error locking camera"));
    return FALSE;
  }

  /* disable shutter sound */
  gst_droidcamsrc_dev_send_command (dev,
      dev->c.CAMERA_CMD_ENABLE_SHUTTER_SOUND, 0, 0);

  g_rec_mutex_unlock (dev->lock);

  return TRUE;
}

void
gst_droidcamsrc_dev_close (GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev close");

  g_rec_mutex_lock (dev->lock);

  if (dev->cam) {
    droid_media_camera_disconnect (dev->cam);
    dev->cam = NULL;
    dev->queue = NULL;
  }

  g_rec_mutex_unlock (dev->lock);
}

void
gst_droidcamsrc_dev_destroy (GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev destroy");

  dev->cam = NULL;
  dev->queue = NULL;
  dev->info = NULL;
  gst_object_unref (dev->wrap_allocator);
  dev->wrap_allocator = NULL;

  gst_object_unref (dev->media_allocator);
  dev->media_allocator = NULL;

  g_mutex_clear (&dev->vid->lock);
  g_cond_clear (&dev->vid->cond);

  gst_object_unref (dev->pool);

  gst_droidcamsrc_recorder_destroy (dev->recorder);

  g_slice_free (GstDroidCamSrcImageCaptureState, dev->img);
  g_slice_free (GstDroidCamSrcVideoCaptureState, dev->vid);
  g_slice_free (GstDroidCamSrcDev, dev);
  dev = NULL;
}

gboolean
gst_droidcamsrc_dev_init (GstDroidCamSrcDev * dev)
{
  GstStructure *config;

  GST_DEBUG ("dev init");

  g_rec_mutex_lock (dev->lock);

  /* first the buffer pool */
  config = gst_buffer_pool_get_config (dev->pool);
  gst_buffer_pool_config_set_params (config, NULL, 0,
      GST_DROIDCAMSRC_NUM_BUFFERS, GST_DROIDCAMSRC_NUM_BUFFERS);

  if (!gst_buffer_pool_set_config (dev->pool, config)) {
    GST_ERROR ("Failed to configure buffer pool");
    return FALSE;
  }

  /* now the callbacks */
  {
    DroidMediaCameraCallbacks cb;

    cb.shutter_cb = gst_droidcamsrc_dev_shutter_callback;
    cb.focus_cb = gst_droidcamsrc_dev_focus_callback;
    cb.focus_move_cb = gst_droidcamsrc_dev_focus_move_callback;
    cb.error_cb = gst_droidcamsrc_dev_error_callback;
    cb.zoom_cb = gst_droidcamsrc_dev_zoom_callback;
    cb.raw_image_cb = gst_droidcamsrc_dev_raw_image_callback;
    cb.compressed_image_cb = gst_droidcamsrc_dev_compressed_image_callback;
    cb.postview_frame_cb = gst_droidcamsrc_dev_postview_frame_callback;
    cb.raw_image_notify_cb = gst_droidcamsrc_dev_raw_image_notify_callback;
    cb.preview_frame_cb = gst_droidcamsrc_dev_preview_frame_callback;
    cb.video_frame_cb = gst_droidcamsrc_dev_video_frame_callback;
    cb.preview_metadata_cb = gst_droidcamsrc_dev_preview_metadata_callback;
    droid_media_camera_set_callbacks (dev->cam, &cb, dev);
  }

  {
    DroidMediaBufferQueueCallbacks cb;
    cb.buffers_released = gst_droidcamsrc_dev_buffers_released;
    cb.frame_available = gst_droidcamsrc_dev_frame_available;
    droid_media_buffer_queue_set_callbacks (dev->queue, &cb, dev);
  }

  gst_droidcamsrc_dev_update_params_locked (dev);

  g_rec_mutex_unlock (dev->lock);

  return TRUE;
}

void
gst_droidcamsrc_dev_deinit (GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev deinit");

  g_rec_mutex_lock (dev->lock);

  if (dev->params) {
    gst_droidcamsrc_params_destroy (dev->params);
    dev->params = NULL;
  }

  g_rec_mutex_unlock (dev->lock);
}

gboolean
gst_droidcamsrc_dev_start (GstDroidCamSrcDev * dev, gboolean apply_settings)
{
  gboolean ret = FALSE;
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  g_rec_mutex_lock (dev->lock);

  if (dev->running) {
    GST_WARNING_OBJECT (src, "preview is already running");
    ret = TRUE;
    goto out;
  }

  GST_DEBUG_OBJECT (src, "dev start");

  if (!gst_buffer_pool_set_active (dev->pool, TRUE)) {
    GST_ERROR_OBJECT (src, "Failed to activate buffer pool");
    goto out;
  }

  if (apply_settings) {
    gst_droidcamsrc_apply_mode_settings (src, SET_ONLY);
  }

  /* now set params */
  if (!gst_droidcamsrc_dev_set_params (dev)) {
    goto out;
  }

  if (dev->use_raw_data) {
    GST_INFO_OBJECT (src, "Using raw data mode");
    droid_media_camera_set_preview_callback_flags (dev->cam,
        dev->c.CAMERA_FRAME_CALLBACK_FLAG_CAMERA);
  } else {
    GST_INFO_OBJECT (src, "Using native buffers mode");
    droid_media_camera_set_preview_callback_flags (dev->cam,
        dev->c.CAMERA_FRAME_CALLBACK_FLAG_NOOP);
  }

  if (!droid_media_camera_start_preview (dev->cam)) {
    GST_ERROR_OBJECT (src, "error starting preview");
    goto out;
  }

  dev->running = TRUE;

  ret = TRUE;

out:
  if (ret != TRUE) {
    gst_buffer_pool_set_active (dev->pool, FALSE);
  }

  g_rec_mutex_unlock (dev->lock);
  return ret;
}

void
gst_droidcamsrc_dev_stop (GstDroidCamSrcDev * dev)
{
  g_rec_mutex_lock (dev->lock);

  GST_DEBUG ("dev stop");

  if (dev->running) {
    GST_DEBUG ("stopping preview");
    gst_buffer_pool_set_active (dev->pool, FALSE);
    droid_media_camera_stop_preview (dev->cam);
    dev->running = FALSE;
    GST_DEBUG ("stopped preview");
  }

  /* Now we need to empty the queue */
  g_mutex_lock (&dev->vfsrc->lock);
  g_queue_foreach (dev->vfsrc->queue, (GFunc) gst_buffer_unref, NULL);
  g_queue_clear (dev->vfsrc->queue);
  g_mutex_unlock (&dev->vfsrc->lock);

  g_rec_mutex_unlock (dev->lock);
}

gboolean
gst_droidcamsrc_dev_set_params (GstDroidCamSrcDev * dev)
{
  bool err;
  gboolean ret = FALSE;
  gchar *params;

  g_rec_mutex_lock (dev->lock);
  if (!dev->cam) {
    GST_ERROR ("camera device is not open");
    goto out;
  }

  if (!dev->params) {
    GST_ERROR ("camera device is not initialized");
    goto out;
  }

  if (!gst_droidcamsrc_params_is_dirty (dev->params)) {
    GST_DEBUG ("no need to reset params");
    ret = TRUE;
    goto out;
  }

  params = gst_droidcamsrc_params_to_string (dev->params);
  GST_LOG ("setting parameters %s", params);
  err = droid_media_camera_set_parameters (dev->cam, params);
  g_free (params);

  if (!err) {
    GST_ERROR ("error setting parameters");
    goto out;
  }

  ret = TRUE;

out:
  g_rec_mutex_unlock (dev->lock);

  return ret;
}

gboolean
gst_droidcamsrc_dev_capture_image (GstDroidCamSrcDev * dev)
{
  gboolean ret = FALSE;
  int msg_type = dev->c.CAMERA_MSG_SHUTTER | dev->c.CAMERA_MSG_RAW_IMAGE
      | dev->c.CAMERA_MSG_POSTVIEW_FRAME | dev->c.CAMERA_MSG_COMPRESSED_IMAGE;

  GST_DEBUG ("dev capture image");

  g_rec_mutex_lock (dev->lock);

  dev->img->image_preview_sent = FALSE;
  dev->img->image_start_sent = FALSE;

  if (!droid_media_camera_take_picture (dev->cam, msg_type)) {
    GST_ERROR ("error capturing image");
    goto out;
  }

  ret = TRUE;

out:
  g_rec_mutex_unlock (dev->lock);
  return ret;
}

gboolean
gst_droidcamsrc_dev_start_video_recording (GstDroidCamSrcDev * dev)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));
  gboolean ret = FALSE;

  GST_DEBUG ("dev start video recording");

  gst_buffer_pool_set_flushing (dev->pool, TRUE);

  g_mutex_lock (&dev->vidsrc->lock);
  dev->vidsrc->pushed_buffers = 0;
  g_mutex_unlock (&dev->vidsrc->lock);

  g_rec_mutex_lock (dev->lock);
  if (dev->use_raw_data) {
    GST_ELEMENT_ERROR (src, STREAM, FORMAT, ("Cannot record video in raw mode"),
        (NULL));
    goto out;
  }

  dev->vid->running = TRUE;
  dev->vid->eos_sent = FALSE;
  dev->vid->video_frames = 0;
  dev->vid->queued_frames = 0;

  if (dev->use_recorder) {
    ret = gst_droidcamsrc_dev_start_video_recording_recorder_locked (dev);
  } else {
    ret = gst_droidcamsrc_dev_start_video_recording_raw_locked (dev);
  }

  if (!ret)
    goto out;

  ret = TRUE;

out:
  g_rec_mutex_unlock (dev->lock);

  gst_buffer_pool_set_flushing (dev->pool, FALSE);

  return ret;
}

void
gst_droidcamsrc_dev_stop_video_recording (GstDroidCamSrcDev * dev)
{
  GST_DEBUG ("dev stop video recording");

  gst_buffer_pool_set_flushing (dev->pool, TRUE);

  /* We need to make sure that some buffers have been pushed */
  g_mutex_lock (&dev->vid->lock);
  while (dev->vid->video_frames <= 4) {
    g_cond_wait (&dev->vid->cond, &dev->vid->lock);
  }
  g_mutex_unlock (&dev->vid->lock);

  /* Now stop pushing to the pad */
  g_rec_mutex_lock (dev->lock);
  dev->vid->running = FALSE;
  g_rec_mutex_unlock (dev->lock);

  /* now make sure nothing is being pushed to the queue */
  g_mutex_lock (&dev->vid->lock);
  g_mutex_unlock (&dev->vid->lock);

  /* our pad task is either sleeping or still pushing buffers. We empty the queue. */
  g_mutex_lock (&dev->vidsrc->lock);
  g_queue_foreach (dev->vidsrc->queue, (GFunc) gst_buffer_unref, NULL);
  g_queue_clear (dev->vidsrc->queue);
  g_mutex_unlock (&dev->vidsrc->lock);

  /* now we are done. We just push eos */
  GST_DEBUG ("Pushing EOS");
  if (!gst_pad_push_event (dev->vidsrc->pad, gst_event_new_eos ())) {
    GST_ERROR ("failed to push EOS event");
  }

  if (!dev->use_recorder) {
    g_rec_mutex_lock (dev->lock);

    GST_INFO ("waiting for queued frames %i", dev->vid->queued_frames);

    while (dev->vid->queued_frames > 0) {
      GST_INFO ("waiting for queued frames to reach 0 from %i",
          dev->vid->queued_frames);
      g_rec_mutex_unlock (dev->lock);
      usleep (VIDEO_RECORDING_STOP_TIMEOUT);
      g_rec_mutex_lock (dev->lock);
    }

    /* TODO: move this unlock() call after we stop recording? */
    g_rec_mutex_unlock (dev->lock);
  }

  if (dev->use_recorder) {
    gst_droidcamsrc_recorder_stop (dev->recorder);
  } else {
    droid_media_camera_stop_recording (dev->cam);
  }

  gst_buffer_pool_set_flushing (dev->pool, FALSE);

  GST_INFO ("dev stopped video recording");
}

static void
gst_droidcamsrc_dev_release_recording_frame (void *data,
    GstDroidCamSrcDevVideoData * video_data)
{
  GstDroidCamSrcDev *dev;

  GST_DEBUG ("dev release recording frame %p", data);

  dev = video_data->dev;

  g_rec_mutex_lock (dev->lock);
  --dev->vid->queued_frames;

  droid_media_camera_release_recording_frame (dev->cam, video_data->data);

  g_slice_free (GstDroidCamSrcDevVideoData, video_data);
  g_rec_mutex_unlock (dev->lock);
}

void
gst_droidcamsrc_dev_update_params_locked (GstDroidCamSrcDev * dev)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));
  gchar *params;

  params = droid_media_camera_get_parameters (dev->cam);

  if (!params) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL),
        ("Failed to read camera parameters"));
    return;
  }

  if (dev->params) {
    /* TODO: is this really needed? We might lose some unset params if we do that. */
    gst_droidcamsrc_params_reload (dev->params, params);
  } else {
    dev->params = gst_droidcamsrc_params_new (params);
  }

  free (params);
}

void
gst_droidcamsrc_dev_update_params (GstDroidCamSrcDev * dev)
{
  g_rec_mutex_lock (dev->lock);
  gst_droidcamsrc_dev_update_params_locked (dev);
  g_rec_mutex_unlock (dev->lock);
}

gboolean
gst_droidcamsrc_dev_start_autofocus (GstDroidCamSrcDev * dev)
{
  gboolean ret = FALSE;

  g_rec_mutex_lock (dev->lock);

  if (!dev->cam) {
    GST_WARNING ("cannot autofocus because camera is not running");
    goto out;
  }

  if (!droid_media_camera_start_auto_focus (dev->cam)) {
    GST_WARNING ("error starting autofocus");
    goto out;
  }

  ret = TRUE;

out:
  g_rec_mutex_unlock (dev->lock);

  return ret;
}

void
gst_droidcamsrc_dev_stop_autofocus (GstDroidCamSrcDev * dev)
{
  g_rec_mutex_lock (dev->lock);

  if (dev->cam) {
    if (!droid_media_camera_cancel_auto_focus (dev->cam))
      GST_WARNING ("error stopping autofocus");
  }

  g_rec_mutex_unlock (dev->lock);
}

/* TODO: the name is not descriptive to what the function does. */
gboolean
gst_droidcamsrc_dev_enable_face_detection (GstDroidCamSrcDev * dev,
    gboolean enable)
{
  gboolean res = FALSE;

  GST_LOG ("enable face detection %d", enable);

  g_rec_mutex_lock (dev->lock);
  if (!dev->cam) {
    GST_WARNING ("camera is not running yet");
    goto out;
  }

  if (!droid_media_camera_enable_face_detection (dev->cam,
          DROID_MEDIA_CAMERA_FACE_DETECTION_HW, enable ? true : false)) {
    GST_ERROR ("error %s face detection", enable ? "enabling" : "disabling");
    goto out;
  }

  res = TRUE;

out:
  g_rec_mutex_unlock (dev->lock);

  return res;
}

gboolean
gst_droidcamsrc_dev_restart (GstDroidCamSrcDev * dev)
{
  gboolean ret = FALSE;

  g_rec_mutex_lock (dev->lock);

  GST_DEBUG ("dev restart");

  if (dev->running) {
    gst_droidcamsrc_dev_stop (dev);
    ret = gst_droidcamsrc_dev_start (dev, TRUE);
  } else {
    ret = TRUE;
  }

  g_rec_mutex_unlock (dev->lock);

  return ret;
}

void
gst_droidcamsrc_dev_send_command (GstDroidCamSrcDev * dev, gint cmd, gint arg1,
    gint arg2)
{
  g_rec_mutex_lock (dev->lock);
  droid_media_camera_send_command (dev->cam, cmd, arg1, arg2);
  g_rec_mutex_unlock (dev->lock);
}

gboolean
gst_droidcamsrc_dev_is_running (GstDroidCamSrcDev * dev)
{
  gboolean ret;

  g_rec_mutex_lock (dev->lock);
  ret = dev->running;
  g_rec_mutex_unlock (dev->lock);

  return ret;
}

static void
gst_droidcamsrc_dev_prepare_buffer (GstDroidCamSrcDev * dev, GstBuffer * buffer,
    DroidMediaRect rect, int width, int height, GstVideoFormat format)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));
  GstVideoCropMeta *crop;

  GST_LOG_OBJECT (src, "prepare buffer %" GST_PTR_FORMAT, buffer);

  gst_droidcamsrc_timestamp (src, buffer);

  crop = gst_buffer_add_video_crop_meta (buffer);
  crop->x = rect.left;
  crop->y = rect.top;
  crop->width = rect.right - rect.left;
  crop->height = rect.bottom - rect.top;

  gst_buffer_add_gst_buffer_orientation_meta (buffer,
      dev->info->orientation, dev->info->direction);

  gst_buffer_add_video_meta (buffer, GST_VIDEO_FRAME_FLAG_NONE,
      format, width, height);

  GST_LOG_OBJECT (src, "preview info: w=%d, h=%d, crop: x=%d, y=%d, w=%d, h=%d",
      width, height, crop->x, crop->y, crop->width, crop->height);
}

static gboolean
gst_droidcamsrc_dev_start_video_recording_recorder_locked (GstDroidCamSrcDev *
    dev)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  if (!gst_droidcamsrc_recorder_init (dev->recorder, dev->cam,
          src->target_bitrate)) {
    GST_ELEMENT_ERROR (src, LIBRARY, FAILED,
        ("error initializing video recorder"), (NULL));
    return FALSE;
  }

  if (!gst_droidcamsrc_recorder_start (dev->recorder)) {
    GST_ELEMENT_ERROR (src, LIBRARY, FAILED, ("error starting video recorder"),
        (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_droidcamsrc_dev_start_video_recording_raw_locked (GstDroidCamSrcDev * dev)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));

  /* TODO: get that from caps */
  if (!droid_media_camera_store_meta_data_in_buffers (dev->cam, true)) {
    GST_ELEMENT_ERROR (src, LIBRARY, SETTINGS,
        ("error storing meta data in buffers for video recording"), (NULL));
    return FALSE;
  }

  if (!droid_media_camera_start_recording (dev->cam)) {
    GST_ELEMENT_ERROR (src, LIBRARY, FAILED, ("error starting video recording"),
        (NULL));
    return FALSE;
  }

  return TRUE;
}

void
gst_droidcamsrc_dev_queue_video_buffer (GstDroidCamSrcDev * dev,
    GstBuffer * buffer)
{
  g_mutex_lock (&dev->vid->lock);
  gst_droidcamsrc_dev_queue_video_buffer_locked (dev, buffer);
  g_mutex_unlock (&dev->vid->lock);
}

static void
gst_droidcamsrc_dev_queue_video_buffer_locked (GstDroidCamSrcDev * dev,
    GstBuffer * buffer)
{
  GstDroidCamSrc *src = GST_DROIDCAMSRC (GST_PAD_PARENT (dev->imgsrc->pad));
  gboolean drop_buffer;

  GST_BUFFER_OFFSET (buffer) = dev->vid->video_frames;
  GST_BUFFER_OFFSET_END (buffer) = ++dev->vid->video_frames;

  g_rec_mutex_lock (dev->lock);
  ++dev->vid->queued_frames;
  g_rec_mutex_unlock (dev->lock);

  drop_buffer = !dev->vid->running;

  if (drop_buffer) {
    GST_INFO_OBJECT (src,
        "dropping buffer because video recording is not running");
    gst_buffer_unref (buffer);
  } else {
    g_mutex_lock (&dev->vidsrc->lock);
    g_queue_push_tail (dev->vidsrc->queue, buffer);
    g_cond_signal (&dev->vidsrc->cond);
    g_mutex_unlock (&dev->vidsrc->lock);
  }

  /* in case stop_video_recording() is waiting for us */
  g_cond_signal (&dev->vid->cond);
}
