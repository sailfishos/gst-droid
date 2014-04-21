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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstdroidcamsrcparams.h"
#include <stdlib.h>
#include "gst/memory/gstgralloc.h"
#include "plugin.h"
#include <gst/memory/gstwrappedmemory.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

static void
gst_droidcamsrc_params_parse (GstDroidCamSrcParams * params, const char *part)
{
  gchar **parts = g_strsplit (part, "=", 2);
  gchar *key = parts[0];
  gchar *value = key ? parts[1] : NULL;

  if (!key || !value) {
    goto out;
  }

  GST_LOG ("param %s = %s", key, value);
  g_hash_table_insert (params->params, g_strdup (key), g_strdup (value));

out:
  g_strfreev (parts);
}

static int
gst_droidcamsrc_params_get_int_locked (GstDroidCamSrcParams * params,
    const char *key)
{
  gchar *value = g_hash_table_lookup (params->params, key);
  if (!value) {
    return -1;
  }

  return atoi (value);
}

int
gst_droidcamsrc_params_get_int (GstDroidCamSrcParams * params, const char *key)
{
  int value;

  g_mutex_lock (&params->lock);
  value = gst_droidcamsrc_params_get_int_locked (params, key);
  g_mutex_unlock (&params->lock);

  return value;
}

float
gst_droidcamsrc_params_get_float (GstDroidCamSrcParams * params,
    const char *key)
{
  gchar *value;
  float result = 0.0;

  g_mutex_lock (&params->lock);

  value = g_hash_table_lookup (params->params, key);

  if (value) {
    result = strtof (value, NULL);
  }

  g_mutex_unlock (&params->lock);

  return result;
}

static gboolean
gst_droidcamsrc_params_parse_dimension (char *d, int *w, int *h)
{
  char **sizes = g_strsplit (d, "x", -1);
  *w = sizes && sizes[0] ? atoi (sizes[0]) : -1;
  *h = *w != -1 && sizes[1] ? atoi (sizes[1]) : -1;

  g_strfreev (sizes);

  return *w != -1 && *h != -1;
}

void
gst_droidcamsrc_params_reload_locked (GstDroidCamSrcParams * params,
    const gchar * str)
{
  gchar **parts = g_strsplit (str, ";", -1);
  gchar **part = parts;

  GST_INFO ("params reload");

  if (params->params) {
    g_hash_table_unref (params->params);
  }

  params->params = g_hash_table_new_full (g_str_hash, g_str_equal,
      (GDestroyNotify) g_free, (GDestroyNotify) g_free);

  while (*part) {
    gst_droidcamsrc_params_parse (params, *part);
    ++part;
  }

  g_strfreev (parts);

  if (params->is_dirty) {
    GST_ERROR ("reloading discarded unset parameters");
  }

  params->is_dirty = FALSE;
}

GstDroidCamSrcParams *
gst_droidcamsrc_params_new (const gchar * params)
{
  GstDroidCamSrcParams *param = g_slice_new0 (GstDroidCamSrcParams);
  g_mutex_init (&param->lock);

  GST_INFO ("params new");

  gst_droidcamsrc_params_reload_locked (param, params);

  return param;
}

void
gst_droidcamsrc_params_destroy (GstDroidCamSrcParams * params)
{
  GST_DEBUG ("params destroy");

  g_mutex_clear (&params->lock);
  g_hash_table_unref (params->params);
  g_slice_free (GstDroidCamSrcParams, params);
}

void
gst_droidcamsrc_params_reload (GstDroidCamSrcParams * params, const gchar * str)
{
  g_mutex_lock (&params->lock);

  gst_droidcamsrc_params_reload_locked (params, str);

  g_mutex_unlock (&params->lock);
}

gchar *
gst_droidcamsrc_params_to_string (GstDroidCamSrcParams * params)
{
  gchar *string = NULL;
  GHashTableIter iter;
  gpointer key, value;

  g_mutex_lock (&params->lock);

  g_hash_table_iter_init (&iter, params->params);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    gchar *str = g_strdup_printf ("%s=%s", (gchar *) key, (gchar *) value);

    if (string == NULL) {
      string = str;
    } else {
      gchar *new_string = g_strjoin (";", string, str, NULL);
      g_free (string);
      g_free (str);
      string = new_string;
    }
  }

  params->is_dirty = FALSE;

  g_mutex_unlock (&params->lock);

  return string;
}

gboolean
gst_droidcamsrc_params_is_dirty (GstDroidCamSrcParams * params)
{
  gboolean is_dirty;

  g_mutex_lock (&params->lock);
  is_dirty = params->is_dirty;
  g_mutex_unlock (&params->lock);

  return is_dirty;
}

static GstCaps *
gst_droidcamsrc_params_get_caps_locked (GstDroidCamSrcParams * params,
    const gchar * key, const gchar * media, const gchar * features,
    const gchar * format)
{
  gchar *value;
  GstCaps *caps = gst_caps_new_empty ();
  gchar **vals;
  gchar **tmp;
  int fps;

  fps = gst_droidcamsrc_params_get_int_locked (params, "preview-frame-rate");

  if (fps == -1) {
    return caps;
  }

  value = g_hash_table_lookup (params->params, key);
  if (!value) {
    return caps;
  }

  vals = g_strsplit (value, ",", -1);
  tmp = vals;
  if (!vals || !vals[0]) {
    return caps;
  }

  while (*tmp) {
    int w, h;
    GstCaps *caps2;
    if (gst_droidcamsrc_params_parse_dimension (*tmp, &w, &h)) {
      caps2 = gst_caps_new_simple (media,
          "width", G_TYPE_INT, w,
          "height", G_TYPE_INT, h,
          "framerate", GST_TYPE_FRACTION, fps, 1, NULL);

      if (format) {
        gst_caps_set_simple (caps2, "format", G_TYPE_STRING, format, NULL);
      }

      if (features) {
        gst_caps_set_features (caps2, 0, gst_caps_features_new (features,
                NULL));
      }

      caps = gst_caps_merge (caps, caps2);
    }

    ++tmp;
  }

  g_strfreev (vals);


  return caps;
}

GstCaps *
gst_droidcamsrc_params_get_viewfinder_caps (GstDroidCamSrcParams * params)
{
  GstCaps *caps;

  g_mutex_lock (&params->lock);
  caps = gst_droidcamsrc_params_get_caps_locked (params, "preview-size-values",
      "video/x-raw", GST_CAPS_FEATURE_MEMORY_DROID_HANDLE, "ENCODED");
  g_mutex_unlock (&params->lock);

  return caps;
}

GstCaps *
gst_droidcamsrc_params_get_video_caps (GstDroidCamSrcParams * params)
{
  GstCaps *caps;

  g_mutex_lock (&params->lock);
  caps = gst_droidcamsrc_params_get_caps_locked (params, "video-size-values",
      "video/x-raw", GST_CAPS_FEATURE_MEMORY_DROID_VIDEO_META_DATA, "ENCODED");
  g_mutex_unlock (&params->lock);

  return caps;
}

GstCaps *
gst_droidcamsrc_params_get_image_caps (GstDroidCamSrcParams * params)
{
  GstCaps *caps;

  g_mutex_lock (&params->lock);
  caps = gst_droidcamsrc_params_get_caps_locked (params, "picture-size-values",
      "image/jpeg", NULL, NULL);
  g_mutex_unlock (&params->lock);

  return caps;
}

void
gst_droidcamsrc_params_set_string (GstDroidCamSrcParams * params,
    const gchar * key, const gchar * value)
{
  gchar *val;

  GST_DEBUG ("setting param %s to %s", key, value);

  g_mutex_lock (&params->lock);
  val = g_hash_table_lookup (params->params, key);

  /* update only if not equal */
  if (g_strcmp0 (val, value)) {
    g_hash_table_insert (params->params, g_strdup (key), g_strdup (value));
    params->is_dirty = TRUE;
  }

  g_mutex_unlock (&params->lock);
}
