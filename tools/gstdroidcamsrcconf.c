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

#include "common.h"
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/interfaces/photography.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>             /* strlen() */
#include <unistd.h>             /* write() */
#include <stdio.h>              /* perror() */

#define ADD_ENTRY(val, droid) {#val, val, droid}

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
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_VIVID, "vivid"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_COLORSWAP, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_SOLARIZE, "solarize"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_OUT_OF_FOCUS, NULL),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_SKY_BLUE, "still-sky-blue"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_GRASS_GREEN, "still-grass-green"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_SKIN_WHITEN, "still-skin-whiten-medium"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_POSTERIZE, "posterize"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_WHITEBOARD, "whiteboard"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_BLACKBOARD, "blackboard"),
      ADD_ENTRY (GST_PHOTOGRAPHY_COLOR_TONE_MODE_AQUA, "aqua"),
      {NULL, -1}
    }},
  {"iso-values", "iso-speed", {
      ADD_ENTRY (0, "auto"),
      ADD_ENTRY (0, "iso-auto"),
      ADD_ENTRY (100, "ISO100"),
      ADD_ENTRY (100, "iso-100"),
      ADD_ENTRY (200, "ISO200"),
      ADD_ENTRY (200, "iso-200"),
      ADD_ENTRY (400, "ISO400"),
      ADD_ENTRY (400, "iso-400"),
      ADD_ENTRY (800, "ISO800"),
      ADD_ENTRY (800, "iso-800"),
      ADD_ENTRY (1600, "ISO1600"),
      ADD_ENTRY (3200, "ISO3200"),
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

static const gchar *
params_get_string (GHashTable * params, const gchar * key)
{
  return g_hash_table_lookup (params, key);
}

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
write_configuration (Common * c, gchar * out, int dev)
{
  int fd;
  gboolean ret;
  GHashTable *params;

  fd = open (out, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd == -1) {
    perror ("open");
    return FALSE;
  }

  if (!add_line (fd, g_strdup_printf ("# configuration for device %d\n", dev))) {
    goto error;
  }

  g_object_get (c->cam_src, "device-parameters", &params, NULL);
  if (!params) {
    g_print ("Failed to get device parameters\n");
    goto error;
  }

  struct Node *n = Nodes;
  while (n->droid) {
    const gchar *p = params_get_string (params, n->droid);

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

gchar *out = NULL;
int dev = 0;

static void
pipeline_started (Common * c)
{
  int ret = 0;

  if (!write_configuration (c, out, dev)) {
    ret = 1;
  }

  common_quit (c, ret);
}

int
main (int argc, char *argv[])
{
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

  dev = atoi (argv[1]);
  out = argv[2];

  Common *common = common_init (&argc, &argv, "camerabin");
  if (!common) {
    return 1;
  }

  common_set_device_mode (common, dev, IMAGE);

  common->started = pipeline_started;

  if (!common_run (common)) {
    return 1;
  }

  return common_destroy (common, TRUE);
}
