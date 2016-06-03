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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/pbutils/encoding-profile.h>
#include <gst/pbutils/encoding-target.h>
#include "common.h"

#define CREATE_AND_ADD(c,m,n,p) do {			\
    c->m = gst_element_factory_make (n, NULL);		\
    if (!c->m) {					\
      g_print ("Failed to create element %s\n", n);	\
      goto error;					\
    }							\
    							\
    gst_object_ref (c->m);				\
    g_object_set (c->bin, p, c->m, NULL);		\
  } while (0)

static gboolean
bus_watch (GstBus * bus, GstMessage * message, gpointer user_data)
{
  Common *c = (Common *) user_data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *debug;
      gst_message_parse_error (message, &err, &debug);
      g_print ("error: %s (%s)\n", err->message, debug);
      g_error_free (err);
      g_free (debug);
      common_quit (c, 1);
    }
      break;

    case GST_MESSAGE_STATE_CHANGED:{
      GstState oldState, newState, pending;
      if (GST_ELEMENT (GST_MESSAGE_SRC (message)) != c->bin) {
        break;
      }

      gst_message_parse_state_changed (message, &oldState, &newState, &pending);
      if (oldState == GST_STATE_PAUSED && newState == GST_STATE_PLAYING
          && pending == GST_STATE_VOID_PENDING) {
        if (c->started) {
          c->started (c);
        }
      }
    }
      break;

    default:
      break;
  }

  return TRUE;
}

static GstEncodingContainerProfile *
common_create_encoding_profile (char *container_name, char *container_caps,
    char *video_caps, char *audio_caps)
{
  GstEncodingVideoProfile *vp;
  GstEncodingAudioProfile *ap;
  GstCaps *caps = gst_caps_from_string (container_caps);
  GstEncodingContainerProfile *p =
      gst_encoding_container_profile_new (container_name,
      container_name,
      caps, NULL);
  gst_caps_unref (caps);

  caps = gst_caps_from_string (video_caps);
  vp = gst_encoding_video_profile_new (caps, NULL, NULL, 1);
  // TODO: This is needed otherwise videorate barfs.
  gst_encoding_video_profile_set_variableframerate (vp, TRUE);
  gst_encoding_container_profile_add_profile (p, (GstEncodingProfile *) vp);
  gst_caps_unref (caps);

  if (audio_caps) {
    caps = gst_caps_from_string (audio_caps);
    ap = gst_encoding_audio_profile_new (caps, NULL, NULL, 1);
    gst_encoding_container_profile_add_profile (p, (GstEncodingProfile *) ap);
    gst_caps_unref (caps);
  }

  return p;
}

Common *
common_init (int *argc, char ***argv, char *bin)
{
  Common *common = NULL;
  GstBus *bus = NULL;
  int flags;
  GstCaps *caps;
  GstEncodingContainerProfile *p;

  common = g_malloc0 (sizeof (Common));
  common->loop = g_main_loop_new (NULL, FALSE);

  gst_init (argc, argv);

  common->bin = gst_element_factory_make (bin, NULL);
  if (!common->bin) {
    g_print ("Failed to create element %s\n", bin);
    goto error;
  }

  flags = 0x00000001            /* no-audio-conversion - Do not use audio conversion elements */
      | 0x00000002              /* no-video-conversion - Do not use video conversion elements */
      | 0x00000004              /* no-viewfinder-conversion - Do not use viewfinder conversion elements */
      | 0x00000008;             /* no-image-conversion - Do not use image conversion elements */

  g_object_set (common->bin, "flags", flags, "mode", 2, NULL);

  CREATE_AND_ADD (common, cam_src, "droidcamsrc", "camera-source");
  CREATE_AND_ADD (common, sink, "droideglsink", "viewfinder-sink");
  CREATE_AND_ADD (common, aud_src, "pulsesrc", "audio-source");

  bus = gst_pipeline_get_bus (GST_PIPELINE (common->bin));
  gst_bus_add_watch (bus, bus_watch, common);
  gst_object_unref (bus);
  bus = NULL;

  /* audio capture caps */
  caps =
      gst_caps_from_string
      ("audio/x-raw, format=S16LE, rate=(int)48000, channels=(int)2");
  g_object_set (common->bin, "audio-capture-caps", caps, NULL);
  gst_caps_unref (caps);

  /* Now our encoding profiles */
  p = common_create_encoding_profile ("MP4 Container",
      "video/quicktime, variant=(string)iso", "video/x-h264",
      "audio/mpeg, mpegversion=(int)4");
  g_object_set (common->bin, "video-profile", p, NULL);
  g_object_unref (p);

  p = common_create_encoding_profile ("JPEG Container", "image/jpeg",
      "image/jpeg", NULL);
  g_object_set (common->bin, "image-profile", p, NULL);
  g_object_unref (p);

  goto out;

error:
  common_destroy (common, TRUE);
  common = NULL;

out:
  return common;
}

int
common_destroy (Common * common, gboolean deinit)
{
  int ret = common->ret;

  if (common->cam_src) {
    gst_object_unref (common->cam_src);
  }

  if (common->aud_src) {
    gst_object_unref (common->aud_src);
  }

  if (common->sink) {
    gst_object_unref (common->sink);
  }

  if (common->bin) {
    gst_object_unref (common->bin);
  }

  if (common->loop) {
    g_main_loop_unref (common->loop);
  }

  g_free (common);

  if (deinit) {
    gst_deinit ();
  }

  return ret;
}

gboolean
common_run (Common * c)
{
  if (gst_element_set_state (c->bin,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_print ("Failed to start pipeline\n");
    return FALSE;
  }

  g_main_loop_run (c->loop);
  gst_element_set_state (c->bin, GST_STATE_NULL);
  return TRUE;
}

static void
common_set_caps (Common * c, char *prop, char *feature, int w, int h, int fps_n,
    int fps_d)
{
  GstCaps *caps;
  gchar *str;

  if (fps_n == -1 || fps_d == -1) {
    str =
        g_strdup_printf ("video/x-raw(%s), width=%d, height=%d", feature, w, h);
  } else {
    str =
        g_strdup_printf
        ("video/x-raw(%s), width=%d, height=%d, framerate=(fraction)%d/%d",
        feature, w, h, fps_n, fps_d);
  }

  caps = gst_caps_from_string (str);
  g_free (str);
  g_object_set (c->bin, prop, caps, NULL);
  gst_caps_unref (caps);
}

void
common_set_vf_caps (Common * c, int w, int h, int fps_n, int fps_d)
{
  common_set_caps (c, "viewfinder-caps", "memory:DroidMediaBuffer", w, h, fps_n,
      fps_d);
}

void
common_set_video_caps (Common * c, int w, int h, int fps_n, int fps_d)
{
  common_set_caps (c, "video-capture-caps", "memory:DroidVideoMetaData", w, h,
      fps_n, fps_d);
}

void
common_set_device_mode (Common * c, Device dev, Mode m)
{
  g_object_set (c->bin, "mode", m, NULL);
  g_object_set (c->cam_src, "camera-device", dev, NULL);
}

void
common_quit (Common * c, int ret)
{
  c->ret = ret;
  g_main_loop_quit (c->loop);
}
