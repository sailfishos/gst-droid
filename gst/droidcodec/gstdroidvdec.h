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

#ifndef __GST_DROID_V_DEC_H__
#define __GST_DROID_V_DEC_H__

#include <gst/gst.h>
#include <gst/video/gstvideodecoder.h>
#include "gst/droid/gstdroidcodec.h"
#include "droidmediaconvert.h"

G_BEGIN_DECLS

#define GST_TYPE_DROIDVDEC \
  (gst_droidvdec_get_type())
#define GST_DROIDVDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DROIDVDEC, GstDroidVDec))
#define GST_DROIDVDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DROIDVDEC, GstDroidVDecClass))
#define GST_IS_DROIDVDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DROIDVDEC))
#define GST_IS_DROIDVDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DROIDVDEC))

typedef struct _GstDroidVDec GstDroidVDec;
typedef struct _GstDroidVDecClass GstDroidVDecClass;
typedef enum _GstDroidVDecState GstDroidVDecState;

enum _GstDroidVDecState
{
  GST_DROID_VDEC_STATE_OK,
  GST_DROID_VDEC_STATE_ERROR,
  GST_DROID_VDEC_STATE_WAITING_FOR_EOS,
  GST_DROID_VDEC_STATE_EOS,
};

struct _GstDroidVDec
{
  GstVideoDecoder parent;
  DroidMediaCodec *codec;
  DroidMediaBufferQueue *queue;
  GstAllocator *allocator;
  GstDroidCodec *codec_type;

  /* eos handling */
  GstDroidVDecState state;
  GMutex state_lock;
  GCond state_cond;

  GstBufferPool *pool;

  /* protected by decoder stream lock */
  GstFlowReturn downstream_flow_ret;
  GstBuffer *codec_data;
  gboolean dirty;
  DroidMediaRect crop_rect;
  gboolean running;
  GstVideoFormat format;

  gsize codec_reported_height;
  gsize codec_reported_width;

  GstVideoCodecState *in_state;
  GstVideoCodecState *out_state;
  DroidMediaConvert *convert;
};

struct _GstDroidVDecClass
{
  GstVideoDecoderClass parent_class;
};

GType gst_droidvdec_get_type (void);

G_END_DECLS

#endif /* __GST_DROID_V_DEC_H__ */
