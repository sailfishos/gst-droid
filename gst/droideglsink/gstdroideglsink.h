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

typedef struct _GstDroidEglSink GstDroidEglSink;
typedef struct _GstDroidEglSinkClass GstDroidEglSinkClass;

struct _GstDroidEglSink
{
  GstVideoSink parent;

  gint fps_n;
  gint fps_d;

  GstBuffer *acquired_buffer;
  GstBuffer *last_buffer;
  EGLDisplay dpy;
  EGLImageKHR image;
  EGLSyncKHR sync;
  GMutex lock;

  GstAllocator *allocator;

  PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
  PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
  PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;
  PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
};

struct _GstDroidEglSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_droideglsink_get_type (void);

G_END_DECLS

#endif /* __GST_DROID_EGL_SINK_H__ */
