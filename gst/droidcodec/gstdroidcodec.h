/*
 * gst-droid
 *
 * Copyright (C) 2014-2015 Mohammed Sameer <msameer@foolab.org>
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

#ifndef __GST_DROID_CODEC_H__
#define __GST_DROID_CODEC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "droidmediacodec.h"

G_BEGIN_DECLS

/* quirks */
#define USE_CODEC_SUPPLIED_HEIGHT_NAME    "use-codec-supplied-height"
#define USE_CODEC_SUPPLIED_HEIGHT_VALUE   0x1

#define USE_CODEC_SUPPLIED_WIDTH_NAME    "use-codec-supplied-width"
#define USE_CODEC_SUPPLIED_WIDTH_VALUE   0x2

typedef struct _GstDroidCodec GstDroidCodec;
typedef struct _GstDroidCodecInfo GstDroidCodecInfo;
typedef struct _GstDroidCodecPrivate GstDroidCodecPrivate;
typedef enum _GstDroidCodecType GstDroidCodecType;
typedef enum _GstDroidCodecCodecDataResult GstDroidCodecCodecDataResult;

enum _GstDroidCodecType
{
  GST_DROID_CODEC_DECODER_AUDIO,
  GST_DROID_CODEC_ENCODER_AUDIO,
  GST_DROID_CODEC_DECODER_VIDEO,
  GST_DROID_CODEC_ENCODER_VIDEO,
};

enum _GstDroidCodecCodecDataResult
{
  GST_DROID_CODEC_CODEC_DATA_OK,
  GST_DROID_CODEC_CODEC_DATA_NOT_NEEDED,
  GST_DROID_CODEC_CODEC_DATA_ERROR,
};

struct _GstDroidCodec {
  GstMiniObject parent;

  GstDroidCodecInfo *info;

  GstDroidCodecPrivate *data;

  gint quirks;
};

GType gst_droid_codec_get_type (void);

static inline GstDroidCodec *gst_droid_codec_ref (GstDroidCodec * codec)
{
  return (GstDroidCodec *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (codec));
}

static inline void gst_droid_codec_unref (GstDroidCodec * codec)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (codec));
}

GstDroidCodec *gst_droid_codec_new_from_caps (GstCaps * caps, GstDroidCodecType type);

GstCaps *gst_droid_codec_get_all_caps (GstDroidCodecType type);
const gchar *gst_droid_codec_get_droid_type (GstDroidCodec * codec);

void gst_droid_codec_complement_caps (GstDroidCodec *codec, GstCaps * caps);
GstBuffer *gst_droid_codec_create_encoder_codec_data (GstDroidCodec *codec, DroidMediaData *data);

GstDroidCodecCodecDataResult gst_droid_codec_create_decoder_codec_data (GstDroidCodec *codec,
									GstBuffer *data,
									DroidMediaData *out,
									GstBuffer *frame_data);

gboolean gst_droid_codec_prepare_decoder_frame (GstDroidCodec * codec, GstVideoCodecFrame * frame,
						DroidMediaData * data,
						DroidMediaBufferCallbacks *cb);

GstBuffer *gst_droid_codec_prepare_encoded_data (GstDroidCodec * codec, DroidMediaData * in);

gboolean gst_droid_codec_process_decoder_data (GstDroidCodec * codec, GstBuffer * buffer,
					       DroidMediaData * out);
gint gst_droid_codec_get_samples_per_frane (GstCaps * caps);

G_END_DECLS

#endif /* __GST_DROID_CODEC_H__ */
