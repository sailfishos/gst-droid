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

#include <OMX_Core.h>
#include <OMX_Component.h>

G_BEGIN_DECLS

#define GST_TYPE_DROID_CODEC (gst_droid_codec_get_type())

typedef struct _GstDroidCodec GstDroidCodec;
typedef struct _GstDroidComponent GstDroidComponent;
typedef struct _GstDroidCodecHandle GstDroidCodecHandle;

struct _GstDroidComponent
{
  GstDroidCodecHandle *handle;

  int in_port;
  int out_port;
  OMX_ERRORTYPE (*get_handle) (OMX_HANDLETYPE * handle,
			       OMX_STRING name, OMX_PTR data, OMX_CALLBACKTYPE * callbacks);
  OMX_ERRORTYPE (*free_handle) (OMX_HANDLETYPE handle);
};

struct _GstDroidCodec
{
  GstMiniObject parent;

  GMutex lock;
  GHashTable *cores;
};

GstDroidCodec *gst_droid_codec_get (void);

GstDroidComponent *gst_droid_codec_get_component (GstDroidCodec * codec, const gchar *type);
void gst_droid_codec_put_component (GstDroidCodec * codec, const gchar * type);

G_END_DECLS

#endif /* __GST_DROID_CODEC_H__ */
