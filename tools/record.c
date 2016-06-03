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

#include <gst/gst.h>
#include <assert.h>
#include "common.h"

gboolean
stop_capture (Common * c)
{
  g_print ("stop recording\n");

  g_signal_emit_by_name (c->bin, "stop-capture", NULL);

  common_quit (c, 0);

  return FALSE;
}

static void
pipeline_started (Common * c)
{
  g_print ("start recording\n");

  g_timeout_add_seconds (5, (GSourceFunc) stop_capture, c);

  g_signal_emit_by_name (c->bin, "start-capture", NULL);
}

int
main (int argc, char *argv[])
{
  Common *common = common_init (&argc, &argv, "camerabin");
  if (!common) {
    return 1;
  }

  common_set_vf_caps (common, 1280, 720, 30, 1);
  common_set_video_caps (common, 1280, 720, 30, 1);

  common_set_device_mode (common, PRIMARY, VIDEO);

  common->started = pipeline_started;

  if (!common_run (common)) {
    return 1;
  }

  return common_destroy (common, TRUE);
}
