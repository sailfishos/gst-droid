/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer
 * Copyright (C) 2015 Jolla Ltd.
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

#include "gstdroidvideotexturesink.h"
#include <gst/video/video.h>
#include <gst/interfaces/nemoeglimagememory.h>
#include <gst/interfaces/nemovideotexture.h>
#include "gst/droid/gstdroidmediabuffer.h"
#include "gst/droid/gstdroidbufferpool.h"

GST_DEBUG_CATEGORY_EXTERN (gst_droid_videotexturesink_debug);
#define GST_CAT_DEFAULT gst_droid_videotexturesink_debug

static void
gst_droidvideotexturesink_video_texture_init (NemoGstVideoTextureClass * iface);

#define gst_droidvideotexturesink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDroidVideoTextureSink, gst_droidvideotexturesink,
    GST_TYPE_DROIDEGLSINK, G_IMPLEMENT_INTERFACE (NEMO_GST_TYPE_VIDEO_TEXTURE,
        gst_droidvideotexturesink_video_texture_init));

enum
{
  PROP_0,
  PROP_EGL_DISPLAY
};

static void
gst_droidvideotexturesink_destroy_sync (GstDroidVideoTextureSink * sink)
{
  GST_DEBUG_OBJECT (sink, "destroy sync %p", sink->sync);

  if (sink->sync) {
    sink->eglDestroySyncKHR (sink->dpy, sink->sync);
    sink->sync = NULL;
  }
}

static void
gst_droidvideotexturesink_wait_sync (GstDroidVideoTextureSink * sink)
{
  GST_DEBUG_OBJECT (sink, "wait sync %p", sink->sync);

  if (sink->sync) {
    /* We will behave like Android does */
    EGLint result =
        sink->eglClientWaitSyncKHR (sink->dpy, sink->sync, 0, EGL_FOREVER_KHR);
    if (result == EGL_FALSE) {
      GST_WARNING_OBJECT (sink, "error 0x%x waiting for fence", eglGetError ());
    } else if (result == EGL_TIMEOUT_EXPIRED_KHR) {
      GST_WARNING_OBJECT (sink, "timeout waiting for fence");
    }

    gst_droidvideotexturesink_destroy_sync (sink);
  }
}

static gboolean
gst_droidvideotexturesink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstDroidVideoTextureSink *sink;
  GstVideoSink *vsink;
  GstVideoInfo info;

  sink = GST_DROIDVIDEOTEXTURESINK (bsink);
  vsink = GST_VIDEO_SINK (bsink);

  GST_DEBUG_OBJECT (sink, "set caps with %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ELEMENT_ERROR (sink, STREAM, FORMAT, (NULL),
        ("Could not locate image format from caps %" GST_PTR_FORMAT, caps));
    return FALSE;
  }

  sink->fps_n = info.fps_n;
  sink->fps_d = info.fps_d;

  GST_VIDEO_SINK_WIDTH (vsink) = info.width;
  GST_VIDEO_SINK_HEIGHT (vsink) = info.height;

  return TRUE;
}

static void
gst_droidvideotexturesink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstDroidVideoTextureSink *sink;

  sink = GST_DROIDVIDEOTEXTURESINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf)) {
      *end = *start + GST_BUFFER_DURATION (buf);
    } else {
      if (sink->fps_n > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND, sink->fps_d, sink->fps_n);
      }
    }
  }
}

static gboolean
gst_droidvideotexturesink_start (GstBaseSink * bsink)
{
  GstDroidVideoTextureSink *sink;

  sink = GST_DROIDVIDEOTEXTURESINK (bsink);

  GST_DEBUG_OBJECT (sink, "start");

  sink->fps_n = 0;
  sink->fps_d = 1;

  sink->image = EGL_NO_IMAGE_KHR;
  sink->sync = NULL;
  sink->eglDestroyImageKHR = NULL;
  sink->eglClientWaitSyncKHR = NULL;
  sink->eglDestroySyncKHR = NULL;

  return TRUE;
}

static gboolean
gst_droidvideotexturesink_stop (GstBaseSink * bsink)
{
  GstDroidVideoTextureSink *sink;

  sink = GST_DROIDVIDEOTEXTURESINK (bsink);

  GST_DEBUG_OBJECT (sink, "stop");

  if (sink->sync) {
    gst_droidvideotexturesink_destroy_sync (sink);
  }

  g_mutex_lock (&sink->lock);

  if (sink->image) {
    GST_WARNING_OBJECT (sink, "destroying leftover EGLImageKHR");
    sink->eglDestroyImageKHR (sink->dpy, sink->image);
    sink->image = EGL_NO_IMAGE_KHR;
  }

  if (sink->acquired_buffer) {
    GST_WARNING_OBJECT (sink, "freeing leftover acquired buffer");
    gst_buffer_unref (sink->acquired_buffer);
    sink->acquired_buffer = NULL;
  }

  g_mutex_unlock (&sink->lock);

  if (sink->last_buffer) {
    GST_INFO_OBJECT (sink, "freeing leftover last buffer");
    gst_buffer_unref (sink->last_buffer);
    sink->last_buffer = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_droidvideotexturesink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstDroidVideoTextureSink *sink;
  gint n_memory;

  sink = GST_DROIDVIDEOTEXTURESINK (vsink);

  GST_DEBUG_OBJECT (sink, "show frame");

  n_memory = gst_buffer_n_memory (buf);

  if (G_UNLIKELY (n_memory == 0)) {
    GST_WARNING_OBJECT (sink, "received an empty buffer");
    /* we will just drop the buffer without errors */
    return GST_FLOW_OK;
  }

  g_mutex_lock (&sink->lock);
  if (sink->acquired_buffer) {
    GST_INFO_OBJECT (sink,
        "acquired buffer exists. Not replacing current buffer");
    g_mutex_unlock (&sink->lock);
    return GST_FLOW_OK;
  }

  GST_LOG_OBJECT (sink, "replacing buffer %p with buffer %p", sink->last_buffer,
      buf);

  gst_buffer_replace (&sink->last_buffer, buf);

  g_mutex_unlock (&sink->lock);

  nemo_gst_video_texture_frame_ready (NEMO_GST_VIDEO_TEXTURE (sink), 0);

  return GST_FLOW_OK;
}

static gboolean
gst_droidvideotexturesink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstDroidVideoTextureSink *sink;

  sink = GST_DROIDVIDEOTEXTURESINK (bsink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_EOS:
      GST_INFO_OBJECT (sink,
          "emitting frame-ready with -1 after %" GST_PTR_FORMAT, event);
      g_mutex_lock (&sink->lock);

      if (sink->last_buffer) {
        gst_buffer_unref (sink->last_buffer);
        sink->last_buffer = NULL;
      }

      g_mutex_unlock (&sink->lock);
      nemo_gst_video_texture_frame_ready (NEMO_GST_VIDEO_TEXTURE (sink), -1);
      break;

    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
}

static void
gst_droidvideotexturesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDroidVideoTextureSink *sink;

  g_return_if_fail (GST_IS_DROIDVIDEOTEXTURESINK (object));

  sink = GST_DROIDVIDEOTEXTURESINK (object);

  switch (prop_id) {
    case PROP_EGL_DISPLAY:
      g_mutex_lock (&sink->lock);
      sink->dpy = g_value_get_pointer (value);
      g_mutex_unlock (&sink->lock);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_droidvideotexturesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDroidVideoTextureSink *sink;

  g_return_if_fail (GST_IS_DROIDVIDEOTEXTURESINK (object));

  sink = GST_DROIDVIDEOTEXTURESINK (object);

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
gst_droidvideotexturesink_finalize (GObject * object)
{
  GstDroidVideoTextureSink *sink;

  sink = GST_DROIDVIDEOTEXTURESINK (object);

  g_mutex_clear (&sink->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_droidvideotexturesink_init (GstDroidVideoTextureSink * sink)
{
  gst_base_sink_set_last_sample_enabled (GST_BASE_SINK (sink), FALSE);

  sink->fps_n = 0;
  sink->fps_d = 0;
  sink->acquired_buffer = NULL;
  sink->last_buffer = NULL;
  sink->dpy = EGL_NO_DISPLAY;
  sink->image = EGL_NO_IMAGE_KHR;
  sink->sync = NULL;
  g_mutex_init (&sink->lock);
  sink->eglDestroyImageKHR = NULL;
  sink->eglClientWaitSyncKHR = NULL;
  sink->eglDestroySyncKHR = NULL;
}

static void
gst_droidvideotexturesink_class_init (GstDroidVideoTextureSinkClass * klass)
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

  gobject_class->finalize = gst_droidvideotexturesink_finalize;
  gobject_class->set_property = gst_droidvideotexturesink_set_property;
  gobject_class->get_property = gst_droidvideotexturesink_get_property;

  gstbasesink_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_droidvideotexturesink_set_caps);
  gstbasesink_class->get_times =
      GST_DEBUG_FUNCPTR (gst_droidvideotexturesink_get_times);
  gstbasesink_class->start =
      GST_DEBUG_FUNCPTR (gst_droidvideotexturesink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_droidvideotexturesink_stop);
  gstbasesink_class->event =
      GST_DEBUG_FUNCPTR (gst_droidvideotexturesink_event);
  videosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_droidvideotexturesink_show_frame);

  g_object_class_override_property (gobject_class, PROP_EGL_DISPLAY,
      "egl-display");
}

static GstMemory *gst_droidvideotexturesink_get_droid_media_buffer_memory
    (GstDroidVideoTextureSink * sink, GstBuffer * buffer)
{
  int x, num;

  GST_DEBUG_OBJECT (sink, "get droid media buffer memory");

  num = gst_buffer_n_memory (buffer);

  GST_DEBUG_OBJECT (sink, "examining %d memory items", num);

  for (x = 0; x < num; x++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, x);

    if (mem && gst_memory_is_type (mem, GST_ALLOCATOR_DROID_MEDIA_BUFFER)) {
      return mem;
    }
  }

  return NULL;
}

static gboolean
gst_droidvideotexturesink_populate_egl_proc (GstDroidVideoTextureSink * sink)
{
  GST_DEBUG_OBJECT (sink, "populate egl proc");

  if (G_UNLIKELY (!sink->eglDestroyImageKHR)) {
    sink->eglDestroyImageKHR =
        (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress ("eglDestroyImageKHR");
  }

  if (G_UNLIKELY (!sink->eglDestroyImageKHR)) {
    return FALSE;
  }

  if (G_UNLIKELY (!sink->eglClientWaitSyncKHR)) {
    sink->eglClientWaitSyncKHR = (PFNEGLCLIENTWAITSYNCKHRPROC)
        eglGetProcAddress ("eglClientWaitSyncKHR");
  }

  if (G_UNLIKELY (!sink->eglClientWaitSyncKHR)) {
    return FALSE;
  }

  if (G_UNLIKELY (!sink->eglDestroySyncKHR)) {
    sink->eglDestroySyncKHR =
        (PFNEGLDESTROYSYNCKHRPROC) eglGetProcAddress ("eglDestroySyncKHR");
  }

  if (G_UNLIKELY (!sink->eglDestroySyncKHR)) {
    return FALSE;
  }

  return TRUE;
}

/* interfaces */
static gboolean
gst_droidvideotexturesink_acquire_frame (NemoGstVideoTexture * iface)
{
  GstDroidVideoTextureSink *sink;
  gboolean ret = TRUE;
  GstMemory *mem;

  sink = GST_DROIDVIDEOTEXTURESINK (iface);

  GST_DEBUG_OBJECT (sink, "acquire frame");

  g_mutex_lock (&sink->lock);

  if (sink->acquired_buffer) {
    GST_WARNING_OBJECT (sink, "buffer %p already acquired",
        sink->acquired_buffer);
    ret = FALSE;
    goto unlock_and_out;
  }

  if (!sink->last_buffer) {
    GST_WARNING_OBJECT (sink, "no buffers available for acquisition");
    ret = FALSE;
    goto unlock_and_out;
  }

  mem =
      gst_droidvideotexturesink_get_droid_media_buffer_memory (sink,
      sink->last_buffer);

  if (!mem) {
    ret = FALSE;
    GST_WARNING_OBJECT (sink, "no droidmedia buffer available");
    goto unlock_and_out;
  }

  sink->acquired_buffer = gst_buffer_ref (sink->last_buffer);

  if (!sink->acquired_buffer) {
    ret = FALSE;
    GST_INFO_OBJECT (sink, "failed to acquire a buffer");
  }

unlock_and_out:
  g_mutex_unlock (&sink->lock);
  return ret;
}

static gboolean
gst_droidvideotexturesink_bind_frame (NemoGstVideoTexture * iface,
    EGLImageKHR * image)
{
  GstDroidVideoTextureSink *sink;
  gboolean ret = FALSE;
  GstMemory *mem;

  sink = GST_DROIDVIDEOTEXTURESINK (iface);

  GST_DEBUG_OBJECT (sink, "bind frame");

  g_mutex_lock (&sink->lock);

  if (sink->dpy == EGL_NO_DISPLAY) {
    GST_WARNING_OBJECT (sink, "can not bind a frame without an EGLDisplay");
    ret = FALSE;
    goto unlock_and_out;
  }

  if (!gst_droidvideotexturesink_populate_egl_proc (sink)) {
    GST_WARNING_OBJECT (sink, "failed to get needed EGL function pointers");
    ret = FALSE;
    goto unlock_and_out;
  }

  if (!sink->acquired_buffer) {
    GST_WARNING_OBJECT (sink, "no frames have been acquired");
    ret = FALSE;
    goto unlock_and_out;
  }

  /* Now we are ready */

  /* We can safely use peek here because we have an extra ref to the buffer */
  mem =
      gst_droidvideotexturesink_get_droid_media_buffer_memory (sink,
      sink->acquired_buffer);
  g_assert (mem);

  sink->image =
      nemo_gst_egl_image_memory_create_image (mem, sink->dpy, EGL_NO_CONTEXT);

  /* Buffer will not go anywhere so we should be safe to unlock. */
  g_mutex_unlock (&sink->lock);

  *image = sink->image;

  ret = TRUE;
  goto out;

unlock_and_out:
  g_mutex_unlock (&sink->lock);

out:
  return ret;
}

static void
gst_droidvideotexturesink_unbind_frame (NemoGstVideoTexture * iface)
{
  GstDroidVideoTextureSink *sink;

  sink = GST_DROIDVIDEOTEXTURESINK (iface);

  GST_DEBUG_OBJECT (sink, "unbind frame");

  g_mutex_lock (&sink->lock);

  if (sink->image == EGL_NO_IMAGE_KHR) {
    GST_WARNING_OBJECT (sink, "Cannot unbind without a valid EGLImageKHR");
    goto out;
  }

  if (sink->eglDestroyImageKHR (sink->dpy, sink->image) != EGL_TRUE) {
    GST_WARNING_OBJECT (sink, "failed to destroy EGLImageKHR %p", sink->image);
  }

  sink->image = EGL_NO_IMAGE_KHR;

out:
  g_mutex_unlock (&sink->lock);
}

static void
gst_droidvideotexturesink_release_frame (NemoGstVideoTexture * iface,
    EGLSyncKHR sync)
{
  GstDroidVideoTextureSink *sink;

  sink = GST_DROIDVIDEOTEXTURESINK (iface);

  GST_DEBUG_OBJECT (sink, "release frame");

  g_mutex_lock (&sink->lock);

  if (sink->acquired_buffer) {
    gst_buffer_unref (sink->acquired_buffer);
    sink->acquired_buffer = NULL;
  }

  g_mutex_unlock (&sink->lock);

  /* Destroy any previous fence */
  gst_droidvideotexturesink_wait_sync (sink);

  /* move on */
  sink->sync = sync;
}

static gboolean
gst_droidvideotexturesink_get_frame_info (NemoGstVideoTexture * iface,
    NemoGstVideoTextureFrameInfo * info)
{
  GstDroidVideoTextureSink *sink;
  gboolean ret;

  sink = GST_DROIDVIDEOTEXTURESINK (iface);

  GST_DEBUG_OBJECT (sink, "get frame info");

  g_mutex_lock (&sink->lock);

  if (!sink->acquired_buffer->pts) {
    GST_INFO_OBJECT (sink, "no buffer has been acquired");

    ret = FALSE;
    goto unlock_and_out;
  }

  info->pts = sink->acquired_buffer->pts;
  info->dts = sink->acquired_buffer->dts;
  info->duration = sink->acquired_buffer->duration;
  info->offset = sink->acquired_buffer->offset;
  info->offset_end = sink->acquired_buffer->offset_end;

  ret = TRUE;

unlock_and_out:
  g_mutex_unlock (&sink->lock);

  return ret;
}

static GstMeta *
gst_droidvideotexturesink_get_frame_meta (NemoGstVideoTexture * iface,
    GType api)
{
  GstDroidVideoTextureSink *sink;
  GstMeta *meta = NULL;

  sink = GST_DROIDVIDEOTEXTURESINK (iface);

  GST_DEBUG_OBJECT (sink, "get frame meta");

  g_mutex_lock (&sink->lock);

  if (sink->acquired_buffer) {
    meta = gst_buffer_get_meta (sink->acquired_buffer, api);
  }

  g_mutex_unlock (&sink->lock);

  return meta;
}

static void
gst_droidvideotexturesink_video_texture_init (NemoGstVideoTextureClass * iface)
{
  iface->acquire_frame = gst_droidvideotexturesink_acquire_frame;
  iface->bind_frame = gst_droidvideotexturesink_bind_frame;
  iface->unbind_frame = gst_droidvideotexturesink_unbind_frame;
  iface->release_frame = gst_droidvideotexturesink_release_frame;
  iface->get_frame_info = gst_droidvideotexturesink_get_frame_info;
  iface->get_frame_meta = gst_droidvideotexturesink_get_frame_meta;
}
