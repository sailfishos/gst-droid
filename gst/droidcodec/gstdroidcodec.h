/*
 * gst-droid
 *
 * Copyright (C) 2014-2015 Mohammed Sameer <msameer@foolab.org>
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

#ifndef __GST_DROID_CODEC_H__
#define __GST_DROID_CODEC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "droidmediacodec.h"

G_BEGIN_DECLS

typedef struct _GstDroidCodec GstDroidCodec;
typedef struct _GstDroidCodecInfo GstDroidCodecInfo;
typedef enum _GstDroidCodecType GstDroidCodecType;

enum _GstDroidCodecType
{
  GST_DROID_CODEC_DECODER,
  GST_DROID_CODEC_ENCODER,
};

struct _GstDroidCodec {
  GstDroidCodecInfo *info;
};

GstDroidCodec *gst_droid_codec_get_from_caps (GstCaps * caps, GstDroidCodecType type);
void gst_droid_codec_free (GstDroidCodec * codec);

GstCaps *gst_droid_codec_get_all_caps (GstDroidCodecType type);
const gchar *gst_droid_codec_get_droid_type (GstDroidCodec * codec);

void gst_droid_codec_complement_caps (GstDroidCodec *codec, GstCaps * caps);
GstBuffer *gst_droid_codec_create_encoder_codec_data (GstDroidCodec *codec, DroidMediaData *data);

gboolean gst_droid_codec_create_decoder_codec_data (GstDroidCodec *codec, GstBuffer *data,
						    DroidMediaData *out,
						    gpointer *codec_type_data);

gboolean gst_droid_codec_prepare_decoder_frame (GstDroidCodec * codec, GstVideoCodecFrame * frame,
						DroidMediaData * data,
						DroidMediaBufferCallbacks *cb,
						gpointer codec_type_data);

GstBuffer *gst_droid_codec_prepare_encoded_data (GstDroidCodec * codec, DroidMediaData * in);

G_END_DECLS

#endif /* __GST_DROID_CODEC_H__ */
