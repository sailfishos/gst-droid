/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
 * Copyright (C) 2015 Jolla LTD.
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

#ifndef __GST_DROID_DEC_H__
#define __GST_DROID_DEC_H__

#include <gst/gst.h>
#include <gst/video/gstvideodecoder.h>
#include "gstdroidcodec.h"
#include "droidmediaconvert.h"

G_BEGIN_DECLS

#define GST_TYPE_DROIDDEC \
  (gst_droiddec_get_type())
#define GST_DROIDDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DROIDDEC, GstDroidDec))
#define GST_DROIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DROIDDEC, GstDroidDecClass))
#define GST_IS_DROIDDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DROIDDEC))
#define GST_IS_DROIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DROIDDEC))

typedef struct _GstDroidDec GstDroidDec;
typedef struct _GstDroidDecClass GstDroidDecClass;

struct _GstDroidDec
{
  GstVideoDecoder parent;
  DroidMediaCodec *codec;
  DroidMediaBufferQueue *queue;
  GstAllocator *allocator;
  GstDroidCodec *codec_type;

  /* eos handling */
  gboolean eos;
  GMutex eos_lock;
  GCond eos_cond;

  /* protected by decoder stream lock */
  GstFlowReturn downstream_flow_ret;
  GstBuffer *codec_data;
  gboolean dirty;

  GstVideoCodecState *in_state;
  GstVideoCodecState *out_state;
  DroidMediaConvert *convert;
};

struct _GstDroidDecClass
{
  GstVideoDecoderClass parent_class;
};

GType gst_droiddec_get_type (void);

G_END_DECLS

#endif /* __GST_DROID_DEC_H__ */
