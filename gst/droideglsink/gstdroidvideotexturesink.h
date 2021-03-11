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

#ifndef __GST_DROID_VIDEO_TEXTURE_SINK_H__
#define __GST_DROID_VIDEO_TEXTURE_SINK_H__

#include "gstdroideglsink.h"

G_BEGIN_DECLS

#define GST_TYPE_DROIDVIDEOTEXTURESINK \
  (gst_droidvideotexturesink_get_type())
#define GST_DROIDVIDEOTEXTURESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DROIDVIDEOTEXTURESINK, GstDroidVideoTextureSink))
#define GST_DROIDVIDEOTEXTURESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DROIDVIDEOTEXTURESINK, GstDroidVideoTextureSinkClass))
#define GST_IS_DROIDVIDEOTEXTURESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DROIDVIDEOTEXTURESINK))
#define GST_IS_DROIDVIDEOTEXTURESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DROIDVIDEOTEXTURESINK))

typedef struct _GstDroidVideoTextureSink GstDroidVideoTextureSink;
typedef struct _GstDroidVideoTextureSinkClass GstDroidVideoTextureSinkClass;

struct _GstDroidVideoTextureSink
{
  GstDroidEglSink parent;

  gint fps_n;
  gint fps_d;

  GstBuffer *acquired_buffer;
  GstBuffer *last_buffer;
  EGLDisplay dpy;
  EGLImageKHR image;
  EGLSyncKHR sync;
  GMutex lock;

  PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
  PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;
  PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
};

struct _GstDroidVideoTextureSinkClass
{
  GstDroidEglSinkClass parent_class;
};

GType gst_droidvideotexturesink_get_type (void);

G_END_DECLS

#endif /* __GST_DROID_EGL_SINK_H__ */
