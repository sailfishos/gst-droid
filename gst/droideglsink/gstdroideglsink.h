/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer
 * Copyright (C) 2015-2020 Jolla Ltd.
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

#ifndef __GST_DROID_EGL_SINK_H__
#define __GST_DROID_EGL_SINK_H__

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

G_BEGIN_DECLS

#define GST_TYPE_DROIDEGLSINK \
  (gst_droideglsink_get_type())
#define GST_DROIDEGLSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DROIDEGLSINK, GstDroidEglSink))
#define GST_DROIDEGLSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DROIDEGLSINK, GstDroidEglSinkClass))
#define GST_IS_DROIDEGLSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DROIDEGLSINK))
#define GST_IS_DROIDEGLSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DROIDEGLSINK))
#define GST_DROIDEGLSINK_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DROIDEGLSINK, GstDroidEglSinkClass))

typedef struct _GstDroidEglSink GstDroidEglSink;
typedef struct _GstDroidEglSinkClass GstDroidEglSinkClass;

struct _GstDroidEglSink
{
  GstVideoSink parent;

  GstBufferPool *pool;
  gulong invalidated_signal_id;
  EGLDisplay dpy;
  GMutex lock;
};

struct _GstDroidEglSinkClass
{
  GstVideoSinkClass parent_class;

  void (*signal_show_frame)            (GstVideoSink *sink, GstBuffer *buffer);
  void (*signal_buffers_invalidated)   (GstVideoSink *sink);

  void (* buffers_invalidated) (GstDroidEglSink *sink);
};

GType gst_droideglsink_get_type (void);

G_END_DECLS

#endif /* __GST_DROID_EGL_SINK_H__ */
