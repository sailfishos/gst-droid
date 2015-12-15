/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
 * Copyright (C) 2015 Jolla LTD.
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

#include "gstdroideglsink.h"
#include <gst/video/video.h>
#include <gst/interfaces/nemovideotexture.h>
#include "gst/droid/gstdroidmediabuffer.h"

GST_DEBUG_CATEGORY_EXTERN (gst_droid_eglsink_debug);
#define GST_CAT_DEFAULT gst_droid_eglsink_debug

static void gst_droideglsink_video_texture_init (NemoGstVideoTextureClass *
    iface);

#define gst_droideglsink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDroidEglSink, gst_droideglsink, GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (NEMO_GST_TYPE_VIDEO_TEXTURE,
        gst_droideglsink_video_texture_init));

static GstStaticPadTemplate gst_droideglsink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{YV12, NV21}") "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_BUFFER, "{YV12}")));

enum
{
  PROP_0,
  PROP_EGL_DISPLAY
};

static void
gst_droideglsink_destroy_sync (GstDroidEglSink * sink)
{
  GST_DEBUG_OBJECT (sink, "destroy sync %p", sink->sync);

  if (sink->sync) {
    sink->eglDestroySyncKHR (sink->dpy, sink->sync);
    sink->sync = NULL;
  }
}

static void
gst_droideglsink_wait_sync (GstDroidEglSink * sink)
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

    gst_droideglsink_destroy_sync (sink);
  }
}

static GstCaps *
gst_droideglsink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstDroidEglSink *sink;
  GstCaps *caps;

  sink = GST_DROIDEGLSINK (bsink);

  GST_DEBUG_OBJECT (sink, "get caps called with filter caps %" GST_PTR_FORMAT,
      filter);
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
    GST_ELEMENT_ERROR (sink, STREAM, FORMAT, (NULL),
        ("ould not locate image format from caps %" GST_PTR_FORMAT, caps));
    return FALSE;
  }

  sink->fps_n = info.fps_n;
  sink->fps_d = info.fps_d;

  GST_VIDEO_SINK_WIDTH (vsink) = info.width;
  GST_VIDEO_SINK_HEIGHT (vsink) = info.height;

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
            gst_util_uint64_scale_int (GST_SECOND, sink->fps_d, sink->fps_n);
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

  sink->image = EGL_NO_IMAGE_KHR;
  sink->sync = NULL;
  sink->eglCreateImageKHR = NULL;
  sink->eglDestroyImageKHR = NULL;
  sink->eglClientWaitSyncKHR = NULL;
  sink->eglDestroySyncKHR = NULL;

  sink->allocator = gst_droid_media_buffer_allocator_new ();

  return TRUE;
}

static gboolean
gst_droideglsink_stop (GstBaseSink * bsink)
{
  GstDroidEglSink *sink;

  sink = GST_DROIDEGLSINK (bsink);

  GST_DEBUG_OBJECT (sink, "stop");

  if (sink->sync) {
    gst_droideglsink_destroy_sync (sink);
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

  gst_object_unref (sink->allocator);
  sink->allocator = NULL;

  return TRUE;
}

static GstFlowReturn
gst_droideglsink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstDroidEglSink *sink;
  gint n_memory;

  sink = GST_DROIDEGLSINK (vsink);

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
gst_droideglsink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstDroidEglSink *sink;

  sink = GST_DROIDEGLSINK (bsink);

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
gst_droideglsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDroidEglSink *sink;

  g_return_if_fail (GST_IS_DROIDEGLSINK (object));

  sink = GST_DROIDEGLSINK (object);

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
gst_droideglsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDroidEglSink *sink;

  g_return_if_fail (GST_IS_DROIDEGLSINK (object));

  sink = GST_DROIDEGLSINK (object);

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
gst_droideglsink_finalize (GObject * object)
{
  GstDroidEglSink *sink;

  sink = GST_DROIDEGLSINK (object);

  g_mutex_clear (&sink->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_droideglsink_init (GstDroidEglSink * sink)
{
  gst_base_sink_set_last_sample_enabled (GST_BASE_SINK (sink), FALSE);

  sink->fps_n = 0;
  sink->fps_d = 0;
  sink->acquired_buffer = NULL;
  sink->last_buffer = NULL;
  sink->dpy = EGL_NO_DISPLAY;
  sink->image = EGL_NO_IMAGE_KHR;
  sink->sync = NULL;
  sink->allocator = NULL;
  g_mutex_init (&sink->lock);
  sink->eglCreateImageKHR = NULL;
  sink->eglDestroyImageKHR = NULL;
  sink->eglClientWaitSyncKHR = NULL;
  sink->eglDestroySyncKHR = NULL;
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
  gobject_class->set_property = gst_droideglsink_set_property;
  gobject_class->get_property = gst_droideglsink_get_property;

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_droideglsink_get_caps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_droideglsink_set_caps);
  gstbasesink_class->get_times = GST_DEBUG_FUNCPTR (gst_droideglsink_get_times);
  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_droideglsink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_droideglsink_stop);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_droideglsink_event);

  videosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_droideglsink_show_frame);

  g_object_class_override_property (gobject_class, PROP_EGL_DISPLAY,
      "egl-display");
}

static GstMemory *
gst_droideglsink_get_droid_media_buffer_memory (GstDroidEglSink * sink,
    GstBuffer * buffer)
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

static GstBuffer *
gst_droideglsink_copy_buffer (GstDroidEglSink * sink, GstBuffer * buffer)
{
  GstMapInfo info;
  GstVideoInfo format;
  GstBuffer *buff = gst_buffer_new ();
  GstMemory *mem = NULL;
  GstCaps *caps = NULL;
  DroidMediaData data;
  gboolean unmap = FALSE;
  DroidMediaBufferCallbacks cb;

  GST_DEBUG_OBJECT (sink, "copy buffer");

  if (!gst_pad_has_current_caps (GST_BASE_SINK_PAD (sink))) {
    goto free_and_out;
  }

  if (!gst_buffer_copy_into (buff, buffer,
          GST_BUFFER_COPY_FLAGS |
          GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META, 0, -1)) {
    goto free_and_out;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    goto free_and_out;
  }

  unmap = TRUE;

  caps = gst_pad_get_current_caps (GST_BASE_SINK_PAD (sink));
  if (!gst_video_info_from_caps (&format, caps)) {
    goto free_and_out;
  }

  cb.ref = (DroidMediaCallback) gst_buffer_ref;
  cb.unref = (DroidMediaCallback) gst_buffer_unref;
  cb.data = buff;

  data.size = info.size;
  data.data = info.data;

  mem = gst_droid_media_buffer_allocator_alloc_from_data (sink->allocator,
      &format, &data, &cb);

  if (!mem) {
    goto free_and_out;
  }

  gst_buffer_append_memory (buff, mem);
  gst_buffer_unmap (buffer, &info);
  gst_caps_unref (caps);

  return buff;

free_and_out:
  if (unmap) {
    gst_buffer_unmap (buffer, &info);
  }

  gst_buffer_unref (buff);

  if (mem) {
    gst_memory_unref (mem);
  }

  if (caps) {
    gst_caps_unref (caps);
  }

  return NULL;
}

static gboolean
gst_droideglsink_populate_egl_proc (GstDroidEglSink * sink)
{
  GST_DEBUG_OBJECT (sink, "populate egl proc");

  if (G_UNLIKELY (!sink->eglCreateImageKHR)) {
    sink->eglCreateImageKHR =
        (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress ("eglCreateImageKHR");
  }

  if (G_UNLIKELY (!sink->eglCreateImageKHR)) {
    return FALSE;
  }

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
gst_droideglsink_acquire_frame (NemoGstVideoTexture * iface)
{
  GstDroidEglSink *sink;
  gboolean ret = TRUE;
  GstMemory *mem;

  sink = GST_DROIDEGLSINK (iface);

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
      gst_droideglsink_get_droid_media_buffer_memory (sink, sink->last_buffer);

  if (mem) {
    sink->acquired_buffer = gst_buffer_ref (sink->last_buffer);
  } else {
    /* Construct a new buffer */
    sink->acquired_buffer =
        gst_droideglsink_copy_buffer (sink, sink->last_buffer);
  }

  if (!sink->acquired_buffer) {
    ret = FALSE;
    GST_INFO_OBJECT (sink, "failed to acquire a buffer");
  }

unlock_and_out:
  g_mutex_unlock (&sink->lock);
  return ret;
}

static gboolean
gst_droideglsink_bind_frame (NemoGstVideoTexture * iface, EGLImageKHR * image)
{
  GstDroidEglSink *sink;
  gboolean ret = FALSE;
  EGLint eglImgAttrs[] =
      { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE, EGL_NONE };
  GstMemory *mem;

  sink = GST_DROIDEGLSINK (iface);

  GST_DEBUG_OBJECT (sink, "bind frame");

  g_mutex_lock (&sink->lock);

  if (sink->dpy == EGL_NO_DISPLAY) {
    GST_WARNING_OBJECT (sink, "can not bind a frame without an EGLDisplay");
    ret = FALSE;
    goto unlock_and_out;
  }

  if (!gst_droideglsink_populate_egl_proc (sink)) {
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
      gst_droideglsink_get_droid_media_buffer_memory (sink,
      sink->acquired_buffer);
  g_assert (mem);

  sink->image =
      sink->eglCreateImageKHR (sink->dpy, EGL_NO_CONTEXT,
      EGL_NATIVE_BUFFER_ANDROID,
      (EGLClientBuffer) gst_droid_media_buffer_memory_get_buffer (mem),
      eglImgAttrs);

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
gst_droideglsink_unbind_frame (NemoGstVideoTexture * iface)
{
  GstDroidEglSink *sink;

  sink = GST_DROIDEGLSINK (iface);

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
gst_droideglsink_release_frame (NemoGstVideoTexture * iface, EGLSyncKHR sync)
{
  GstDroidEglSink *sink;

  sink = GST_DROIDEGLSINK (iface);

  GST_DEBUG_OBJECT (sink, "release frame");

  g_mutex_lock (&sink->lock);

  if (sink->acquired_buffer) {
    gst_buffer_unref (sink->acquired_buffer);
    sink->acquired_buffer = NULL;
  }

  g_mutex_unlock (&sink->lock);

  /* Destroy any previous fence */
  gst_droideglsink_wait_sync (sink);

  /* move on */
  sink->sync = sync;
}

static gboolean
gst_droideglsink_get_frame_info (NemoGstVideoTexture * iface,
    NemoGstVideoTextureFrameInfo * info)
{
  GstDroidEglSink *sink;
  gboolean ret;

  sink = GST_DROIDEGLSINK (iface);

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
gst_droideglsink_get_frame_meta (NemoGstVideoTexture * iface, GType api)
{
  GstDroidEglSink *sink;
  GstMeta *meta = NULL;

  sink = GST_DROIDEGLSINK (iface);

  GST_DEBUG_OBJECT (sink, "get frame meta");

  g_mutex_lock (&sink->lock);

  if (sink->acquired_buffer) {
    meta = gst_buffer_get_meta (sink->acquired_buffer, api);
  }

  g_mutex_unlock (&sink->lock);

  return meta;
}

static void
gst_droideglsink_video_texture_init (NemoGstVideoTextureClass * iface)
{
  iface->acquire_frame = gst_droideglsink_acquire_frame;
  iface->bind_frame = gst_droideglsink_bind_frame;
  iface->unbind_frame = gst_droideglsink_unbind_frame;
  iface->release_frame = gst_droideglsink_release_frame;
  iface->get_frame_info = gst_droideglsink_get_frame_info;
  iface->get_frame_meta = gst_droideglsink_get_frame_meta;
}
