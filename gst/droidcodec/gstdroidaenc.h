/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
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

#ifndef __GST_DROID_A_ENC_H__
#define __GST_DROID_A_ENC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudioencoder.h>
#include "gst/droid/gstdroidcodec.h"

G_BEGIN_DECLS

#define GST_TYPE_DROIDAENC \
  (gst_droidaenc_get_type())
#define GST_DROIDAENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DROIDAENC, GstDroidAEnc))
#define GST_DROIDAENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DROIDAENC, GstDroidAEncClass))
#define GST_IS_DROIDAENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DROIDAENC))
#define GST_IS_DROIDAENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DROIDAENC))

typedef struct _GstDroidAEnc GstDroidAEnc;
typedef struct _GstDroidAEncClass GstDroidAEncClass;

struct _GstDroidAEnc
{
  GstAudioEncoder parent;
  DroidMediaCodec *codec;
  GstDroidCodec *codec_type;
  gboolean first_frame_sent;

  GstCaps *caps;
  gint channels;
  gint rate;

  gint32 target_bitrate;

  /* eos handling */
  gboolean eos;
  GMutex eos_lock;
  GCond eos_cond;

  /* protected by decoder stream lock */
  GstFlowReturn downstream_flow_ret;
  gboolean dirty;
  gboolean finished;
};

struct _GstDroidAEncClass
{
  GstAudioEncoderClass parent_class;
};

GType gst_droidaenc_get_type (void);

G_END_DECLS

#endif /* __GST_DROID_A_ENC_H__ */
