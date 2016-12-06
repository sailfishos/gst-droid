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

#ifndef __GST_DROID_A_DEC_H__
#define __GST_DROID_A_DEC_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>
#include <gst/base/gstadapter.h>
#include "gst/droid/gstdroidcodec.h"

G_BEGIN_DECLS

#define GST_TYPE_DROIDADEC \
  (gst_droidadec_get_type())
#define GST_DROIDADEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DROIDADEC, GstDroidADec))
#define GST_DROIDADEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DROIDADEC, GstDroidADecClass))
#define GST_IS_DROIDADEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DROIDADEC))
#define GST_IS_DROIDADEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DROIDADEC))

typedef struct _GstDroidADec GstDroidADec;
typedef struct _GstDroidADecClass GstDroidADecClass;

struct _GstDroidADec
{
  GstAudioDecoder parent;
  DroidMediaCodec *codec;
  GstDroidCodec *codec_type;

  gint channels;
  gint rate;

  /* eos handling */
  gboolean eos;
  GMutex eos_lock;
  GCond eos_cond;

  /* protected by decoder stream lock */
  GstFlowReturn downstream_flow_ret;
  GstBuffer *codec_data;
  gboolean dirty;
  gint spf;
  GstAudioInfo *info;
  GstAdapter *adapter;
  gboolean running;
};

struct _GstDroidADecClass
{
  GstAudioDecoderClass parent_class;
};

GType gst_droidadec_get_type (void);

G_END_DECLS

#endif /* __GST_DROID_A_DEC_H__ */
