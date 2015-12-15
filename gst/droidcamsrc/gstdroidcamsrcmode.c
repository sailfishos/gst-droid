/*
 * gst-droid
 *
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "gstdroidcamsrcmode.h"
#include "gstdroidcamsrc.h"

static GstDroidCamSrcMode *gst_droidcamsrc_mode_new (GstDroidCamSrc * src);
static gboolean gst_droidcamsrc_mode_negotiate_pad (GstDroidCamSrcMode * mode,
    GstPad * pad, gboolean force);

static GstDroidCamSrcMode *
gst_droidcamsrc_mode_new (GstDroidCamSrc * src)
{
  GstDroidCamSrcMode *mode = g_slice_new0 (GstDroidCamSrcMode);

  mode->src = src;
  mode->vfsrc = src->vfsrc->pad;
  mode->modesrc = NULL;

  return mode;
}

GstDroidCamSrcMode *
gst_droidcamsrc_mode_new_image (GstDroidCamSrc * src)
{
  GstDroidCamSrcMode *mode = gst_droidcamsrc_mode_new (src);

  mode->modesrc = src->imgsrc->pad;

  return mode;
}

GstDroidCamSrcMode *
gst_droidcamsrc_mode_new_video (GstDroidCamSrc * src)
{
  GstDroidCamSrcMode *mode = gst_droidcamsrc_mode_new (src);

  mode->modesrc = src->vidsrc->pad;

  return mode;
}

void
gst_droidcamsrc_mode_free (GstDroidCamSrcMode * mode)
{
  g_slice_free (GstDroidCamSrcMode, mode);
}

gboolean
gst_droidcamsrc_mode_activate (GstDroidCamSrcMode * mode)
{
  gboolean ret;
  gboolean running;

  g_rec_mutex_lock (&mode->src->dev_lock);

  if (!mode->src->dev) {
    GST_INFO_OBJECT (mode->src, "dev not ready. deferring caps negotiation");

    g_rec_mutex_unlock (&mode->src->dev_lock);
    return TRUE;
  }

  running = gst_droidcamsrc_dev_is_running (mode->src->dev);

  if (running) {
    gst_droidcamsrc_dev_stop (mode->src->dev);
  }

  gst_droidcamsrc_mode_negotiate_pad (mode, mode->modesrc, FALSE);
  gst_droidcamsrc_mode_negotiate_pad (mode, mode->vfsrc, TRUE);

  if (running) {
    ret = gst_droidcamsrc_dev_start (mode->src->dev, FALSE);
  } else {
    ret = gst_droidcamsrc_apply_params (mode->src);
  }

  /* now update max-zoom that we have a preview size */
  gst_droidcamsrc_dev_update_params (mode->src->dev);
  gst_droidcamsrc_update_max_zoom (mode->src);

  g_rec_mutex_unlock (&mode->src->dev_lock);

  return ret;
}

void
gst_droidcamsrc_mode_deactivate (GstDroidCamSrcMode * mode)
{
  /* nothing for now */
}

gboolean
gst_droidcamsrc_mode_pad_is_significant (GstDroidCamSrcMode * mode,
    GstPad * pad)
{
  return mode->vfsrc == pad || mode->modesrc == pad;
}

gboolean
gst_droidcamsrc_mode_negotiate (GstDroidCamSrcMode * mode, GstPad * pad)
{
  gboolean ret;
  gboolean running;

  g_rec_mutex_lock (&mode->src->dev_lock);

  running = gst_droidcamsrc_dev_is_running (mode->src->dev);

  if (running && pad == mode->vfsrc) {
    /* stop preview */
    gst_droidcamsrc_dev_stop (mode->src->dev);
  }

  gst_droidcamsrc_mode_negotiate_pad (mode, pad, FALSE);

  if (pad == mode->vfsrc) {
    /* start which will also apply the settings */
    if (running) {
      ret = gst_droidcamsrc_dev_start (mode->src->dev, FALSE);
    } else {
      ret = gst_droidcamsrc_apply_params (mode->src);
    }

    /* now update max-zoom that we have a preview size */
    gst_droidcamsrc_dev_update_params (mode->src->dev);
    gst_droidcamsrc_update_max_zoom (mode->src);
  } else {
    /* apply settings */
    ret = gst_droidcamsrc_dev_set_params (mode->src->dev);
  }

  g_rec_mutex_unlock (&mode->src->dev_lock);

  return ret;
}

static gboolean
gst_droidcamsrc_mode_negotiate_pad (GstDroidCamSrcMode * mode, GstPad * pad,
    gboolean force)
{
  GstDroidCamSrcPad *data = gst_pad_get_element_private (pad);
  /* take pad lock */
  g_mutex_lock (&data->lock);

  if (!force) {
    if (!gst_pad_check_reconfigure (data->pad)) {
      g_mutex_unlock (&data->lock);
      return TRUE;
    }
  }

  /*
   * stream start
   * we must send it before we send our CAPS event
   */
  if (G_UNLIKELY (data->open_stream)) {
    gchar *stream_id;
    GstEvent *event;

    stream_id =
        gst_pad_create_stream_id (data->pad, GST_ELEMENT_CAST (mode->src),
        GST_PAD_NAME (data->pad));

    GST_DEBUG_OBJECT (pad, "Pushing STREAM_START for pad");
    event = gst_event_new_stream_start (stream_id);
    gst_event_set_group_id (event, gst_util_group_id_next ());
    if (!gst_pad_push_event (data->pad, event)) {
      GST_ERROR_OBJECT (mode->src,
          "failed to push STREAM_START event for pad %s",
          GST_PAD_NAME (data->pad));
    }

    g_free (stream_id);
    data->open_stream = FALSE;
  }

  /* negotiate */
  if (!data->negotiate (data)) {
    GST_ELEMENT_ERROR (mode->src, STREAM, FORMAT, (NULL),
        ("failed to negotiate %s.", GST_PAD_NAME (data->pad)));

    g_mutex_unlock (&data->lock);
    return FALSE;
  }

  /* toss pad queue */
  g_queue_foreach (data->queue, (GFunc) gst_buffer_unref, NULL);
  g_queue_clear (data->queue);

  /* unlock */
  g_mutex_unlock (&data->lock);

  return TRUE;
}
