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

#include <OMX_Core.h>
#include <OMX_Component.h>

G_BEGIN_DECLS

#define GST_TYPE_DROID_CODEC (gst_droid_codec_get_type())

/* stolen from gst-omx */
#define GST_OMX_INIT_STRUCT(st) G_STMT_START { \
  memset ((st), 0, sizeof (*(st))); \
  (st)->nSize = sizeof (*(st)); \
  (st)->nVersion.s.nVersionMajor = 1; \
  (st)->nVersion.s.nVersionMinor = 1; \
  } G_STMT_END

typedef struct _GstDroidCodec GstDroidCodec;
typedef struct _GstDroidComponent GstDroidComponent;
typedef struct _GstDroidCodecHandle GstDroidCodecHandle;
typedef struct _GstDroidComponentPort GstDroidComponentPort;

struct _GstDroidComponent
{
  GstDroidCodecHandle *handle;
  OMX_HANDLETYPE omx;
  GstDroidComponentPort *in_port;
  GstDroidComponentPort *out_port;
  GstElement *parent;
};

struct _GstDroidCodec
{
  GstMiniObject parent;

  GMutex lock;
  GHashTable *cores;
};

struct _GstDroidComponentPort
{
  int usage;
  //  GMutex lock;
  //  GCond cond;
  OMX_PARAM_PORTDEFINITIONTYPE def;
  GstBufferPool *buffers;
  GstAllocator *allocator;
  GstDroidComponent *comp;
};

GstDroidCodec *gst_droid_codec_get (void);

GstDroidComponent *gst_droid_codec_get_component (GstDroidCodec * codec,
						  const gchar *type, GstElement * parent);
void gst_droid_codec_put_component (GstDroidCodec * codec, GstDroidComponent * component);


OMX_ERRORTYPE gst_droid_codec_get_param (GstDroidComponent * comp,
					 OMX_INDEXTYPE index, gpointer param);
OMX_ERRORTYPE gst_droid_codec_set_param (GstDroidComponent * comp,
					 OMX_INDEXTYPE index, gpointer param);
gboolean gst_droid_codec_configure_component (GstDroidComponent *comp,
					      const GstVideoInfo * info);
gboolean gst_droid_codec_start_component (GstDroidComponent * comp, GstCaps * sink, GstCaps * src);

const gchar *gst_omx_error_to_string (OMX_ERRORTYPE err);
const gchar *gst_omx_state_to_string (OMX_STATETYPE state);
const gchar *gst_omx_command_to_string (OMX_COMMANDTYPE cmd);

G_END_DECLS

#endif /* __GST_DROID_CODEC_H__ */
