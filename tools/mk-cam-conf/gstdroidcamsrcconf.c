/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <gst/gst.h>
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/interfaces/photography.h>
#include "gst/droidcamsrc/gstdroidcamsrc.h"
#include "gst/droidcamsrc/gstdroidcamsrcdev.h"
#include "gst/droidcamsrc/gstdroidcamsrcparams.h"

#define ADD_ENTRY(val, droid) {#val, val, droid}

typedef struct
{
  GMainLoop *loop;
  GstElement *bin;
  GstElement *src;
  const gchar *out;
  int dev;
} Data;

typedef struct
{
  const gchar *name;
  gint val;
  const gchar *droid;
} Entry;

struct Node
{
  const gchar *droid;
  const gchar *gst;
  Entry entries[19];
} Nodes[] = {
  /* *INDENT-OFF* */
  {"flash-mode-values", "flash-mode", {
      ADD_ENTRY (GST_PHOTOGRAPHY_FLASH_MODE_AUTO, "auto"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FLASH_MODE_OFF, "off"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FLASH_MODE_ON, "on"),
      {NULL, -1}
    }},
  {"focus-mode-values", "focus-mode", {
      ADD_ENTRY (GST_PHOTOGRAPHY_FOCUS_MODE_AUTO, "auto"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FOCUS_MODE_MACRO, "macro"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FOCUS_MODE_PORTRAIT, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_FOCUS_MODE_INFINITY, "infinity"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FOCUS_MODE_HYPERFOCAL, "fixed"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FOCUS_MODE_EXTENDED, "edof"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL, "continuous"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_EXTENDED, "continuous"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FOCUS_MODE_MANUAL, NULL),
      {NULL, -1}
    }},
  {"whitebalance-values", "white-balance-mode", {
      ADD_ENTRY (GST_PHOTOGRAPHY_WB_MODE_AUTO, "auto"),
      ADD_ENTRY (GST_PHOTOGRAPHY_WB_MODE_DAYLIGHT, "daylight"),
      ADD_ENTRY (GST_PHOTOGRAPHY_WB_MODE_CLOUDY, "cloudy-daylight"),
      ADD_ENTRY (GST_PHOTOGRAPHY_WB_MODE_SUNSET, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_WB_MODE_TUNGSTEN, "incandescent"),
      ADD_ENTRY (GST_PHOTOGRAPHY_WB_MODE_FLUORESCENT, "fluorescent"),
      ADD_ENTRY (GST_PHOTOGRAPHY_WB_MODE_MANUAL, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_WB_MODE_WARM_FLUORESCENT, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_WB_MODE_SHADE, NULL),
      {NULL, -1}
    }},
  {"scene-mode-values", "scene-mode", {
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_MANUAL, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_CLOSEUP, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_PORTRAIT, "portrait"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_LANDSCAPE, "landscape"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_SPORT, "sports"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_NIGHT, "night"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_AUTO, "auto"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_ACTION, "action"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_NIGHT_PORTRAIT, "night-portrait"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_THEATRE, "theatre"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_BEACH, "beach"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_SNOW, "snow"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_SUNSET, "sunset"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_STEADY_PHOTO, "steadyphoto"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_FIREWORKS, "fireworks"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_PARTY, "party"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_CANDLELIGHT, "candlelight"),
      ADD_ENTRY (GST_PHOTOGRAPHY_SCENE_MODE_BARCODE, "barcode"),
      {NULL, -1}
    }},
  {"effect-values", "color-tone-mode", {
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_NORMAL, "none"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_SEPIA, "sepia"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_NEGATIVE, "negative"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_GRAYSCALE, "mono"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_NATURAL, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_VIVID, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_COLORSWAP, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_SOLARIZE, "solarize"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_OUT_OF_FOCUS, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_SKY_BLUE, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_GRASS_GREEN, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_SKIN_WHITEN, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_POSTERIZE, "posterize"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_WHITEBOARD, "whiteboard"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_BLACKBOARD, "blackboard"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_AQUA, "aqua"),
      {NULL, -1}
    }},
  {"iso-values", "iso-speed", {
      ADD_ENTRY (0, "auto"),
      ADD_ENTRY (100, "ISO100"),
      ADD_ENTRY (200, "ISO200"),
      ADD_ENTRY (400, "ISO400"),
      ADD_ENTRY (800, "ISO800"),
      {NULL, -1}
    }},
  {"antibanding-values", "flicker-mode", {
      ADD_ENTRY (GST_PHOTOGRAPHY_FLICKER_REDUCTION_OFF, "off"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FLICKER_REDUCTION_50HZ, "50hz"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FLICKER_REDUCTION_60HZ, "60hz"),
      ADD_ENTRY (GST_PHOTOGRAPHY_FLICKER_REDUCTION_AUTO, "auto"),
      {NULL, -1}
    }},
  {NULL, NULL},
  /* *INDENT-ON* */
};

static gboolean
add_line (int fd, gchar * line)
{
  int len = strlen (line);
  if (write (fd, line, len) != len) {
    perror ("write");
    g_free (line);
    return FALSE;
  }

  if (write (fd, "\n", 1) != 1) {
    perror ("write");
    return FALSE;
  }

  return TRUE;
}

static gboolean
write_section (int fd, struct Node *n, const gchar * params)
{
  Entry *e = n->entries;
  gchar **p;

  add_line (fd, g_strdup_printf ("[%s]", n->gst));

  /* first we add the comments but not for iso-speed */
  if (g_strcmp0 (n->gst, "iso-speed")) {
    while (e->name) {
      if (!add_line (fd, g_strdup_printf ("# %d = %s", e->val, e->name))) {
        return FALSE;
      }
      ++e;
    }
  }

  /* now the actual work */
  g_print ("%s (%s) => %s\n", n->gst, n->droid, params);

  p = g_strsplit (params, ",", -1);

  e = n->entries;
  while (e->name) {
    gchar **tmp = p;
    while (*tmp) {
#if 0
      g_print ("comparing %s and %s\n", e->droid, *tmp);
#endif

      /* focus needs special handling because we use continuous to indicate continuous-picture
       * or continuous-video and droidcamsrc decides depending on the mode */
      if (!g_strcmp0 (*tmp, e->droid) || (!g_strcmp0 (e->droid, "continuous")
              && (!g_strcmp0 (*tmp, "continuous-picture")
                  || !g_strcmp0 (*tmp, "continuous-video")))) {
        if (!add_line (fd, g_strdup_printf ("%d = %s", e->val, e->droid))) {
          g_strfreev (p);
          return FALSE;
        }
        break;
      }
      ++tmp;
    }
    ++e;
  }

  g_strfreev (p);

  write (fd, "\n", 1);

  return TRUE;
}

static gboolean
write_configuration (Data * data)
{
  int fd;
  gboolean ret;
  GstDroidCamSrc *src;
  GstDroidCamSrcParams *params;

  fd = open (data->out, O_WRONLY | O_CREAT,
      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd == -1) {
    perror ("open");
    return FALSE;
  }

  if (!add_line (fd, g_strdup_printf ("# configuration for device %d\n",
              data->dev))) {
    goto error;
  }

  src = (GstDroidCamSrc *) data->src;
  params = src->dev->params;

  struct Node *n = Nodes;
  while (n->droid) {
    const gchar *p = gst_droidcamsrc_params_get_string (params, n->droid);

    if (p) {
      if (!write_section (fd, n, p)) {
        goto error;
      }
    }

    ++n;
  }

out:
  close (fd);
  fd = -1;
  return ret;

error:
  ret = FALSE;
  goto out;
}

static gboolean
bus_watch (GstBus * bus, GstMessage * message, gpointer user_data)
{
  Data *data = (Data *) user_data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *debug;
      gst_message_parse_error (message, &err, &debug);
      g_error ("error: %s (%s)\n", err->message, debug);
      g_error_free (err);
      g_free (debug);
      g_main_loop_quit (data->loop);
    }
      break;

    case GST_MESSAGE_STATE_CHANGED:{
      GstState oldState, newState, pending;
      if (GST_ELEMENT (GST_MESSAGE_SRC (message)) != data->bin) {
        break;
      }

      gst_message_parse_state_changed (message, &oldState, &newState, &pending);
      if (oldState == GST_STATE_READY && newState == GST_STATE_PAUSED
          && pending == GST_STATE_VOID_PENDING) {
        write_configuration (data);

        /* we are done */
        g_main_loop_quit (data->loop);
      }
    }
      break;

    default:
      break;
  }

  return TRUE;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstBus *bus;
  GstElement *src, *bin;
  Data *data;
  int ret = 0;

  if (argc != 3) {
    g_print ("usage: %s <camera device> <output file>\n"
        " Examples:\n"
        "  For primary camera\n"
        "   %s 0 %s/gst-droid/gstdroidcamsrc-0.conf\n"
        "  For secondary camera\n"
        "   %s 1 %s/gst-droid/gstdroidcamsrc-1.conf\n",
        argv[0], argv[0], SYSCONFDIR, argv[0], SYSCONFDIR);
    return 0;
  }

  data = g_malloc (sizeof (Data));
  data->out = argv[2];
  data->dev = atoi (argv[1]);

  loop = g_main_loop_new (NULL, FALSE);
  data->loop = loop;

  gst_init (&argc, &argv);

  src = gst_element_factory_make ("droidcamsrc", NULL);
  if (!src) {
    g_error ("Failed to create an instance of droidcamsrc\n");
    ret = 1;
    goto out;
  }
  data->src = src;
  g_object_set (src, "camera-device", data->dev, NULL);

  bin = gst_pipeline_new (NULL);
  data->bin = bin;

  gst_bin_add (GST_BIN (bin), src);

  bus = gst_pipeline_get_bus (GST_PIPELINE (bin));
  gst_bus_add_watch (bus, bus_watch, data);
  gst_object_unref (bus);
  bus = NULL;

  if (gst_element_set_state (bin, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
    g_error ("error starting pipeline\n");
    ret = 1;
    goto out;
  }

  g_main_loop_run (loop);

  gst_element_set_state (bin, GST_STATE_NULL);

out:
  g_free (data);

  return ret;
}
