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

#ifndef __GST_DROID_ENC_H__
#define __GST_DROID_ENC_H__

#include <gst/gst.h>
#include <gst/video/gstvideoencoder.h>
#include "gstdroidcodec.h"

G_BEGIN_DECLS

#define GST_TYPE_DROIDENC \
  (gst_droidenc_get_type())
#define GST_DROIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DROIDENC, GstDroidEnc))
#define GST_DROIDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DROIDENC, GstDroidEncClass))
#define GST_IS_DROIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DROIDENC))
#define GST_IS_DROIDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DROIDENC))

typedef struct _GstDroidEnc GstDroidEnc;
typedef struct _GstDroidEncClass GstDroidEncClass;

struct _GstDroidEnc
{
  GstVideoEncoder parent;
  DroidMediaCodec *codec;
  GstDroidCodec *codec_type;
  GstVideoCodecState *in_state;
  GstVideoCodecState *out_state;
  gboolean first_frame_sent;
  gint32 target_bitrate;

  /* eos handling */
  gboolean eos;
  GMutex eos_lock;
  GCond eos_cond;

  /* protected by decoder stream lock */
  GstFlowReturn downstream_flow_ret;
  gboolean dirty;

};

struct _GstDroidEncClass
{
  GstVideoEncoderClass parent_class;
};

GType gst_droidenc_get_type (void);

G_END_DECLS

#endif /* __GST_DROID_ENC_H__ */
