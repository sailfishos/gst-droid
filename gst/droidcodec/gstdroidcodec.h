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

typedef enum {
  GST_DROID_CODEC_DECODER,
  GST_DROID_CODEC_ENCODER,
} GstDroidCodecType;

typedef struct {
  GstDroidCodecType type;
  const gchar *mime;
  const gchar *droid;
  gboolean (*validate_structure) (const GstStructure * s);
  void (*complement_caps)(GstCaps * caps);
  const gchar *caps;
  GstBuffer *(*create_encoder_codec_data) (DroidMediaData *data);
  gboolean (*process_encoder_data) (DroidMediaData *in, DroidMediaData *out);
  gboolean (*create_decoder_codec_data) (GstBuffer *data, DroidMediaData *out,
					 gpointer *codec_type_data);
  gboolean (*process_decoder_data) (GstBuffer *buffer, gpointer codec_type_data,
				    DroidMediaData *out);
} GstDroidCodec;

GstDroidCodec *gst_droid_codec_get_from_caps (GstCaps * caps, GstDroidCodecType type);
GstCaps *gst_droid_codec_get_all_caps (GstDroidCodecType type);
gboolean gst_droid_codec_consume_frame (DroidMediaCodec * codec, GstVideoCodecFrame * frame,
					GstClockTime ts);
gboolean gst_droid_codec_consume_frame2 (DroidMediaCodec * codec, GstVideoCodecFrame * frame,
					DroidMediaCodecData *data);

G_END_DECLS

#endif /* __GST_DROID_CODEC_H__ */
