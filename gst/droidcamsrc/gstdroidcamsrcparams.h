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

#ifndef __GST_DROID_CAM_SRC_PARAMS_H__
#define __GST_DROID_CAM_SRC_PARAMS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstDroidCamSrcParams GstDroidCamSrcParams;

struct _GstDroidCamSrcParams
{
  GHashTable *params;
  gboolean is_dirty;
  GMutex lock;
};

GstDroidCamSrcParams * gst_droidcamsrc_params_new (const gchar * params);
void gst_droidcamsrc_params_destroy (GstDroidCamSrcParams *params);
void gst_droidcamsrc_params_reload (GstDroidCamSrcParams *params, const gchar * str);

gchar *gst_droidcamsrc_params_to_string (GstDroidCamSrcParams *params);
gboolean gst_droidcamsrc_params_is_dirty (GstDroidCamSrcParams *params);

GstCaps *gst_droidcamsrc_params_get_viewfinder_caps (GstDroidCamSrcParams *params);
GstCaps *gst_droidcamsrc_params_get_video_caps (GstDroidCamSrcParams *params);
GstCaps *gst_droidcamsrc_params_get_image_caps (GstDroidCamSrcParams *params);

gboolean gst_droidcamsrc_params_set_string (GstDroidCamSrcParams *params, const gchar *key,
					const gchar *value);
int gst_droidcamsrc_params_get_int (GstDroidCamSrcParams * params, const char *key);
float gst_droidcamsrc_params_get_float (GstDroidCamSrcParams * params, const char *key);

G_END_DECLS

#endif /* __GST_DROID_CAM_SRC_PARAMS_H__ */
