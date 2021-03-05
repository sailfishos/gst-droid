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

#include "gstdroidcamsrcparams.h"
#include <stdlib.h>
#include "gst/droid/gstdroidmediabuffer.h"
#include "plugin.h"
#include "gst/droid/gstwrappedmemory.h"
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droid_camsrc_debug);
#define GST_CAT_DEFAULT gst_droid_camsrc_debug

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

gboolean
gst_droidcamsrc_has_param (GstDroidCamSrcParams * params, const char *key)
{
  return g_hash_table_contains (params->params, key);
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
    result = g_ascii_strtod (value, NULL);
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
gst_droidcamsrc_params_fill_fps_range_arrays_locked (GstDroidCamSrcParams *
    params)
{
  gchar *range;
  gchar *val;

  range = g_hash_table_lookup (params->params, "preview-fps-range-values");
  if (!range) {
    GST_ERROR ("no preview-fps-range-values");
    return;
  }

  if (range[0] != '(') {
    GST_ERROR ("invalid preview-fps-range-values");
    return;
  }

  val = range;

  /* this is really a primitive parser but I assume the HAL is providing correct values as
   * it should work with Android */
  while (*val != '\0') {
    int min, max;

    val = strchr (val, '(');
    ++val;
    min = atoi (val);

    val = strchr (val, ',');
    ++val;                      /* bypass , */
    max = atoi (val);
    val = strchr (val, ')');
    ++val;

    if (min == 0 || max == 0) {
      GST_ERROR ("failed to parse preview-fps-range-values");
      continue;
    }

    g_array_append_val (params->min_fps_range, min);
    g_array_append_val (params->max_fps_range, max);

    GST_LOG ("parsed fps range: %d - %d", min, max);
  }
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

  /* now try to extract preview-fps-range-values */
  if (params->min_fps_range) {
    g_array_free (params->min_fps_range, TRUE);
  }
  params->min_fps_range = g_array_new (FALSE, FALSE, sizeof (gint));

  if (params->max_fps_range) {
    g_array_free (params->max_fps_range, TRUE);
  }
  params->max_fps_range = g_array_new (FALSE, FALSE, sizeof (gint));

  gst_droidcamsrc_params_fill_fps_range_arrays_locked (params);

  params->is_dirty = FALSE;
  params->has_separate_video_size_values =
      g_hash_table_lookup (params->params, "video-size-values") != NULL;
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

  if (params->min_fps_range) {
    g_array_free (params->min_fps_range, TRUE);
  }

  if (params->max_fps_range) {
    g_array_free (params->max_fps_range, TRUE);
  }

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
          "width", G_TYPE_INT, w, "height", G_TYPE_INT, h, NULL);

      if (format) {
        gst_caps_set_simple (caps2, "format", G_TYPE_STRING, format, NULL);
      }

      if (features) {
        gst_caps_set_features (caps2, 0, gst_caps_features_new (features,
                NULL));
      }

      /* now add frame rate */
      if (params->min_fps_range->len == 0) {
        /* the easy part first */
        gst_caps_set_simple (caps2, "framerate", GST_TYPE_FRACTION, fps, 1,
            NULL);
        GST_DEBUG ("merging caps %" GST_PTR_FORMAT, caps2);
        caps = gst_caps_merge (caps, caps2);
      } else {
        int x;
        for (x = 0; x < params->min_fps_range->len; x++) {
          GstCaps *caps3;
          int min = g_array_index (params->min_fps_range, gint, x);
          int max = g_array_index (params->max_fps_range, gint, x);
          min /= 1000;
          max /= 1000;

          caps3 = gst_caps_copy (caps2);
          if (min == max) {
            gst_caps_set_simple (caps3, "framerate", GST_TYPE_FRACTION, min, 1,
                NULL);
          } else {
            gst_caps_set_simple (caps3, "framerate", GST_TYPE_FRACTION_RANGE,
                min, 1, max, 1, NULL);
          }

          GST_DEBUG ("merging caps %" GST_PTR_FORMAT, caps3);
          caps = gst_caps_merge (caps, caps3);
        }

        gst_caps_unref (caps2);
      }
    }

    ++tmp;
  }

  g_strfreev (vals);

  return gst_caps_simplify (caps);
}

GstCaps *
gst_droidcamsrc_params_get_viewfinder_caps (GstDroidCamSrcParams * params,
    GstVideoFormat format)
{
  GstCaps *caps;

  g_mutex_lock (&params->lock);
  caps =
      gst_caps_merge (gst_droidcamsrc_params_get_caps_locked (params,
          "preview-size-values", "video/x-raw",
          GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_QUEUE_BUFFER,
          gst_video_format_to_string (format)),
      gst_droidcamsrc_params_get_caps_locked (params, "preview-size-values",
          "video/x-raw", NULL, "NV21"));
  g_mutex_unlock (&params->lock);

  return caps;
}

GstCaps *
gst_droidcamsrc_params_get_video_caps (GstDroidCamSrcParams * params)
{
  GstCaps *caps;

  g_mutex_lock (&params->lock);

  gchar *key =
      params->has_separate_video_size_values ? "video-size-values" :
      "preview-size-values";

  caps = gst_droidcamsrc_params_get_caps_locked (params, key,
      "video/x-raw", GST_CAPS_FEATURE_MEMORY_DROID_VIDEO_META_DATA, "YV12");

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
gst_droidcamsrc_params_set_string_locked (GstDroidCamSrcParams * params,
    const gchar * key, const gchar * value)
{
  gchar *val;

  GST_DEBUG ("setting param %s to %s", key, value);

  val = g_hash_table_lookup (params->params, key);

  /* update only if not equal */
  if (g_strcmp0 (val, value)) {
    g_hash_table_insert (params->params, g_strdup (key), g_strdup (value));
    params->is_dirty = TRUE;
  }
}

void
gst_droidcamsrc_params_set_string (GstDroidCamSrcParams * params,
    const gchar * key, const gchar * value)
{
  g_mutex_lock (&params->lock);
  gst_droidcamsrc_params_set_string_locked (params, key, value);
  g_mutex_unlock (&params->lock);
}

const gchar *
gst_droidcamsrc_params_get_string (GstDroidCamSrcParams * params,
    const char *key)
{
  const gchar *value;

  g_mutex_lock (&params->lock);
  value = g_hash_table_lookup (params->params, key);
  g_mutex_unlock (&params->lock);

  return value;
}

void
gst_droidcamsrc_params_choose_image_framerate (GstDroidCamSrcParams * params,
    GstCaps * caps)
{
  int x;
  int target_min = -1, target_max = -1;

  g_mutex_lock (&params->lock);

  for (x = 0; x < params->min_fps_range->len; x++) {
    int min = g_array_index (params->min_fps_range, gint, x);
    int max = g_array_index (params->max_fps_range, gint, x);

    GstCaps *c = gst_caps_copy (caps);
    if (min == max) {
      gst_caps_set_simple (c, "framerate", GST_TYPE_FRACTION, min / 1000, 1,
          NULL);
    } else {
      gst_caps_set_simple (c, "framerate", GST_TYPE_FRACTION_RANGE,
          min / 1000, 1, max / 1000, 1, NULL);
    }

    if (!gst_caps_can_intersect (caps, c)) {
      gst_caps_unref (c);
      continue;
    }

    gst_caps_unref (c);

    /* the fps we have is valid. Select it if higher than our current target, or wider */
    if (max > target_max || (max == target_max && min < target_min)) {
      target_min = min;
      target_max = max;
    }
  }

  if (target_min != -1 && target_max != -1) {
    gchar *var;

    /* use the max */
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION,
        target_max / 1000, 1, NULL);

    var = g_strdup_printf ("%d,%d", target_min, target_max);
    gst_droidcamsrc_params_set_string_locked (params, "preview-fps-range", var);
    g_free (var);
  }

  g_mutex_unlock (&params->lock);
}

void
gst_droidcamsrc_params_choose_video_framerate (GstDroidCamSrcParams * params,
    GstCaps * caps)
{
  int x;
  int target_min = -1, target_max = -1;

  g_mutex_lock (&params->lock);

  for (x = 0; x < params->min_fps_range->len; x++) {
    int min = g_array_index (params->min_fps_range, gint, x);
    int max = g_array_index (params->max_fps_range, gint, x);

    GstCaps *c = gst_caps_copy (caps);
    if (min == max) {
      gst_caps_set_simple (c, "framerate", GST_TYPE_FRACTION, min / 1000, 1,
          NULL);
    } else {
      gst_caps_set_simple (c, "framerate", GST_TYPE_FRACTION_RANGE,
          min / 1000, 1, max / 1000, 1, NULL);
    }

    if (!gst_caps_can_intersect (caps, c)) {
      gst_caps_unref (c);
      continue;
    }

    gst_caps_unref (c);

    /* the fps we have is valid. Select it if higher than our current target, or narrower */
    if (max > target_max || (max == target_max && min > target_min)) {
      target_min = min;
      target_max = max;
    }
  }

  if (target_min != -1 && target_max != -1) {
    gchar *var;

    /* use the max */
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION,
        target_max / 1000, 1, NULL);

    var = g_strdup_printf ("%d,%d", target_min, target_max);
    gst_droidcamsrc_params_set_string_locked (params, "preview-fps-range", var);
    g_free (var);
  }

  g_mutex_unlock (&params->lock);
}
