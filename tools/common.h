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

#ifndef __COMMON_H__
#define __COMMON_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _Common Common;

struct _Common
{
  GstElement *bin;
  GstElement *cam_src;
  GstElement *aud_src;
  GstElement *sink;
  GMainLoop *loop;

  void (* started) (Common *c);

  int ret;
};

typedef enum {
  PRIMARY = 0,
  SECONDARY = 1,
} Device;

typedef enum {
  IMAGE = 1,
  VIDEO = 2,
} Mode;

Common *common_init (int *argc, char ***argv, char *bin);
int common_destroy (Common *common, gboolean deinit);
gboolean common_run (Common *c);

void common_set_device_mode(Common *c, Device dev, Mode m);

void common_set_vf_caps (Common *c, int w, int h, int fps_n, int fps_d);
void common_set_video_caps (Common *c, int w, int h, int fps_n, int fps_d);

void common_quit (Common *c, int ret);

G_END_DECLS

#endif /* __COMMON_H__ */
