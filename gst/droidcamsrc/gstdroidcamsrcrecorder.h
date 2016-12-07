/*
 * gst-droid
 *
 * Copyright (C) 2016 Jolla LTD.
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

#ifndef __GST_DROIDCAMSRC_RECORDER_H__
#define __GST_DROIDCAMSRC_RECORDER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <droidmediarecorder.h>

G_BEGIN_DECLS

typedef struct _GstDroidCamSrcRecorder GstDroidCamSrcRecorder;
typedef struct _GstDroidCodec GstDroidCodec;
typedef struct _GstDroidCamSrcPad GstDroidCamSrcPad;

struct _GstDroidCamSrcRecorder
{
  /* opaque */

  GstDroidCamSrcPad *vidsrc;
  GstDroidCodec *codec;
  DroidMediaRecorder *recorder;
  DroidMediaCodecEncoderMetaData md;
};

GstDroidCamSrcRecorder *gst_droidcamsrc_recorder_create (GstDroidCamSrcPad *vidsrc);
void gst_droidcamsrc_recorder_destroy (GstDroidCamSrcRecorder *recorder);

gboolean gst_droidcamsrc_recorder_init (GstDroidCamSrcRecorder *recorder, DroidMediaCamera *cam, gint32 target_bitrate);

void gst_droidcamsrc_recorder_update_vid (GstDroidCamSrcRecorder *recorder, GstVideoInfo *info, GstCaps *caps);

gboolean gst_droidcamsrc_recorder_start (GstDroidCamSrcRecorder *recorder);
void gst_droidcamsrc_recorder_stop (GstDroidCamSrcRecorder *recorder);

G_END_DECLS

#endif /* __GST_DROIDCAMSRC_RECORDER_H__ */
