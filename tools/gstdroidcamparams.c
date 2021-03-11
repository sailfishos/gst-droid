/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer
 * Copyright (C) 2015-2021 Jolla Ltd.
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
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>             /* strlen() */
#include <unistd.h>             /* write() */
#include <stdio.h>              /* perror() */

void
print_parameter (gpointer key, gpointer user_data)
{
  g_print ("%s: %s\n", (gchar *)key, (gchar *)g_hash_table_lookup((GHashTable *)user_data, key));
}

static gboolean
print_parameters (Common * c, int dev)
{
  GList *keys;
  GHashTable *params;

  g_object_get (c->cam_src, "device-parameters", &params, NULL);
  if (!params) {
    g_print ("Failed to get device parameters\n");
    return FALSE;
  }

  keys = g_hash_table_get_keys (params);
  keys = g_list_sort (keys, (GCompareFunc)g_ascii_strcasecmp);

  g_list_foreach (keys, print_parameter, params);

  return TRUE;
}

int dev = 0;

static void
pipeline_started (Common * c)
{
  int ret = 0;

  if (!print_parameters (c, dev)) {
    ret = 1;
  }

  common_quit (c, ret);
}

int
main (int argc, char *argv[])
{
  if (argc != 2) {
    g_print ("usage: %s <camera device>\n"
        " Examples:\n"
        "  For primary camera\n"
        "   %s 0\n"
        "  For secondary camera\n"
        "   %s 1\n",
        argv[0], argv[0], argv[0]);
    return 0;
  }

  dev = atoi (argv[1]);

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
