/*
 * gst-droid
 *
 * Copyright (C) 2016 Jolla Ltd.
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

#include "gstdroidfpsdumper.h"

#define DEFAULT_FPS_UPDATE_INTERVAL_MS 1000     /* 1000 ms */

GstDroidFpsDumper *
gst_droid_fps_dumper_new (const gchar * name)
{
  GstDroidFpsDumper *dumper = g_malloc (sizeof (GstDroidFpsDumper));
  dumper->name = g_strdup (name);

  gst_droid_fps_dumper_reset (dumper);

  return dumper;
}

void
gst_droid_fps_dumper_destroy (GstDroidFpsDumper * dumper)
{
  g_free (dumper->name);
  g_free (dumper);
}

void
gst_droid_fps_dumper_reset (GstDroidFpsDumper * dumper)
{
  dumper->update_interval = GST_MSECOND * DEFAULT_FPS_UPDATE_INTERVAL_MS;
  dumper->start_ts = GST_CLOCK_TIME_NONE;
  dumper->interval_ts = GST_CLOCK_TIME_NONE;

  dumper->n_frames = 0;
  dumper->interval_n_frames = 0;
}

void
gst_droid_fps_new_frame (GstDroidFpsDumper * dumper)
{
  GstClockTime ts;

  dumper->n_frames++;
  dumper->interval_n_frames++;

  ts = gst_util_get_timestamp ();

  if (!GST_CLOCK_TIME_IS_VALID (dumper->start_ts)) {
    dumper->start_ts = ts;
    dumper->interval_ts = ts;
  }

  if (GST_CLOCK_DIFF (dumper->interval_ts, ts) > dumper->update_interval) {
    /* show */
    gdouble interval_diff = (ts - dumper->interval_ts) / GST_SECOND;
    gdouble diff = (ts - dumper->start_ts) / GST_SECOND;
    g_print ("%s: interval: %f, average: %f\n", dumper->name,
        dumper->interval_n_frames / interval_diff, dumper->n_frames / diff);

    /* reset */
    dumper->interval_ts = ts;
    dumper->interval_n_frames = 0;
  }
}
