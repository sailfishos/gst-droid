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
  gboolean (*verify) (const GstStructure * s);
  void (*compliment)(GstCaps * caps);
  const gchar *caps;
  gboolean (*construct_encoder_codec_data) (gpointer data, gsize size, GstBuffer **buffer);
  gboolean (*process_encoder_data) (DroidMediaData *in, DroidMediaData *out);
  gboolean (*construct_decoder_codec_data) (GstBuffer *data, DroidMediaData *out);
} GstDroidCodec;

GstDroidCodec *gst_droid_codec_get_from_caps (GstCaps * caps, GstDroidCodecType type);
GstCaps *gst_droid_codec_get_all_caps (GstDroidCodecType type);
gboolean gst_droid_codec_consume_frame (DroidMediaCodec * codec, GstVideoCodecFrame * frame, GstClockTime ts);

G_END_DECLS

#endif /* __GST_DROID_CODEC_H__ */
