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

#ifndef __GST_DROID_FPS_DUMPER_H__
#define __GST_DROID_FPS_DUMPER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstDroidFpsDumper GstDroidFpsDumper;

struct _GstDroidFpsDumper {
  gchar *name;
  GstClockTime update_interval;
  GstClockTime interval_ts;
  GstClockTime start_ts;
  gint n_frames;
  gint interval_n_frames;
};

GstDroidFpsDumper *gst_droid_fps_dumper_new (const gchar *name);
void gst_droid_fps_dumper_destroy (GstDroidFpsDumper *dumper);

void gst_droid_fps_dumper_reset (GstDroidFpsDumper *dumper);

void gst_droid_fps_new_frame (GstDroidFpsDumper *dumper);

G_END_DECLS

#endif /* __GST_DROID_FPS_DUMPER_H__ */
