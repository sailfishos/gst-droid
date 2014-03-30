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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstdroiddec.h"
#include "gst/memory/gstgralloc.h"
#include "gstdroidcodectype.h"

#define gst_droiddec_parent_class parent_class
G_DEFINE_TYPE (GstDroidDec, gst_droiddec, GST_TYPE_VIDEO_DECODER);

GST_DEBUG_CATEGORY_STATIC (gst_droid_dec_debug);
#define GST_CAT_DEFAULT gst_droid_dec_debug

static GstStaticPadTemplate gst_droiddec_src_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_SURFACE, "{ENCODED, YV12}")));

static GstStaticPadTemplate gst_droiddec_sink_template_factory =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
gst_droiddec_finalize (GObject * object)
{
  GstDroidDec *dec = GST_DROIDDEC (object);

  GST_DEBUG_OBJECT (dec, "finalize");

  gst_mini_object_unref (GST_MINI_OBJECT (dec->codec));
  // TODO:

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_droiddec_open (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "open");

  // TODO:

  return TRUE;
}

static gboolean
gst_droiddec_close (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "close");


  // TODO:

  return TRUE;
}

static gboolean
gst_droiddec_start (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "start");

  // TODO:

  return TRUE;
}

static gboolean
gst_droiddec_stop (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "stop");

  // TODO:

  return TRUE;
}

static gboolean
gst_droiddec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  const gchar *type;
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "set format %" GST_PTR_FORMAT, state->caps);

  if (dec->comp) {
    return FALSE;
  }

  type = gst_droid_codec_type_from_caps (state->caps);
  if (!type) {
    return FALSE;
  }

  dec->comp = gst_droid_codec_get_component (dec->codec, type);
  if (!dec->comp) {
    return FALSE;
  }

  dec->in_state = gst_video_codec_state_ref (state);

  /* configure codec */
  if (!gst_droid_codec_configure_component (dec->comp, &state->info)) {
    return FALSE;
  }

  gst_video_decoder_set_output_state (GST_VIDEO_DECODER (dec),
      GST_VIDEO_FORMAT_ENCODED, state->info.width, state->info.height,
      dec->in_state);

  /* now start */
  if (!gst_droid_codec_start_component (dec->comp)) {
    return FALSE;
  }
  // TODO:

  return TRUE;
}

static gboolean
gst_droiddec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "reset %d", hard);

  // TODO:

  return TRUE;
}

static GstFlowReturn
gst_droiddec_finish (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "finish");

  // TODO:

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_droiddec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "handle frame");

  // TODO:

  return GST_FLOW_OK;
}

#if 0
static gboolean
gst_droiddec_sink_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "sink event %" GST_PTR_FORMAT, event);

  // TODO:

  return TRUE;
}

static gboolean
gst_droiddec_src_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "src event %" GST_PTR_FORMAT, event);

  // TODO:

  return TRUE;
}
#endif

static gboolean
gst_droiddec_negotiate (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "negotiate");

  // TODO:

  return TRUE;
}

static gboolean
gst_droiddec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "decide allocation %" GST_PTR_FORMAT, query);

  // TODO:

  return TRUE;
}

static gboolean
gst_droiddec_propose_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "propose allocation %" GST_PTR_FORMAT, query);

  // TODO:

  return TRUE;
}

static gboolean
gst_droiddec_flush (GstVideoDecoder * decoder)
{
  GstDroidDec *dec = GST_DROIDDEC (decoder);

  GST_DEBUG_OBJECT (dec, "flush");

  // TODO:

  return TRUE;
}

static void
gst_droiddec_init (GstDroidDec * dec)
{
  dec->codec = gst_droid_codec_get ();
  dec->comp = NULL;
  dec->in_state = NULL;
  dec->out_state = NULL;

  // TODO:
}

static void
gst_droiddec_class_init (GstDroidDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideoDecoderClass *gstvideodecoder_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstvideodecoder_class = (GstVideoDecoderClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Video sink", "Decoder/Video/Device",
      "Android HAL decoder", "Mohammed Sameer <msameer@foolab.org>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droiddec_sink_template_factory));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droiddec_src_template_factory));

  gobject_class->finalize = gst_droiddec_finalize;
  gstvideodecoder_class->open = GST_DEBUG_FUNCPTR (gst_droiddec_open);
  gstvideodecoder_class->close = GST_DEBUG_FUNCPTR (gst_droiddec_close);
  gstvideodecoder_class->start = GST_DEBUG_FUNCPTR (gst_droiddec_start);
  gstvideodecoder_class->stop = GST_DEBUG_FUNCPTR (gst_droiddec_stop);
  gstvideodecoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_droiddec_set_format);
  gstvideodecoder_class->reset = GST_DEBUG_FUNCPTR (gst_droiddec_reset);
  gstvideodecoder_class->finish = GST_DEBUG_FUNCPTR (gst_droiddec_finish);
  gstvideodecoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_droiddec_handle_frame);
  /*
     gstvideodecoder_class->sink_event =
     GST_DEBUG_FUNCPTR (gst_droiddec_sink_event);
     gstvideodecoder_class->src_event = GST_DEBUG_FUNCPTR (gst_droiddec_src_event);
   */
  gstvideodecoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_droiddec_negotiate);
  gstvideodecoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_droiddec_decide_allocation);
  gstvideodecoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_droiddec_propose_allocation);
  gstvideodecoder_class->flush = GST_DEBUG_FUNCPTR (gst_droiddec_flush);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_droid_dec_debug, "droiddec",
      0, "Android HAL decoder");

  return gst_element_register (plugin, "droiddec", GST_RANK_PRIMARY + 1,
      GST_TYPE_DROIDDEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    droiddec,
    "Android HAL decoder",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, "http://foolab.org/")
