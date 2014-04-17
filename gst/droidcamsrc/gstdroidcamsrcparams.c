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
#include <string.h>
#include "plugin.h"
#include <gst/memory/gstwrappedmemory.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

static void
gst_droidcamsrc_params_destroy_list (gpointer data)
{
  GList *list = (GList *) data;

  g_list_free_full (list, (GDestroyNotify) g_free);
}

static void
gst_droidcamsrc_params_parse_key_values (GstDroidCamSrcParams * params,
    const char *key, const char *values)
{
  char **value = g_strsplit (values, ",", -1);
  char **val = value;
  GList *list = NULL;

  while (*val) {
    list = g_list_append (list, g_strdup (*val));
    ++val;
  }

  g_hash_table_insert (params->params, g_strdup (key), list);

  g_strfreev (value);
}

static void
gst_droidcamsrc_params_parse_key_value (GstDroidCamSrcParams * params,
    const char *key, const char *value)
{
  if (g_strrstr (value, ",")) {
    /* needs farther splitting */
    gst_droidcamsrc_params_parse_key_values (params, key, value);
  } else {
    /* we are done */
    g_hash_table_insert (params->params, g_strdup (key), g_list_append (NULL,
            g_strdup (value)));
  }
}

static void
gst_droidcamsrc_params_parse (GstDroidCamSrcParams * params, const char *part)
{
  gchar **parts = g_strsplit (part, "=", 2);
  gchar *key = parts[0];
  gchar *value = key ? parts[1] : NULL;

  GST_LOG ("param %s = %s", key, value);

  if (!key || !value) {
    goto out;
  }

  gst_droidcamsrc_params_parse_key_value (params, key, value);

out:
  g_strfreev (parts);
}

static GList *
gst_droidcamsrc_params_get_item_locked (GstDroidCamSrcParams * params,
    const char *key)
{
  GList *list = g_hash_table_lookup (params->params, key);
  if (!list) {
    return NULL;
  }

  return list;
}

#if 0
static gchar *
gst_droidcamsrc_params_get_string_locked (GstDroidCamSrcParams * params,
    const char *key)
{
  GList *list = gst_droidcamsrc_params_get_item_locked (params, key);
  if (!list) {
    return NULL;
  }

  return list->data;
}
#endif

static int
gst_droidcamsrc_params_get_int_locked (GstDroidCamSrcParams * params,
    const char *key)
{
  GList *list = gst_droidcamsrc_params_get_item_locked (params, key);
  if (!list) {
    return -1;
  }

  return atoi (list->data);
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
      (GDestroyNotify) g_free,
      (GDestroyNotify) gst_droidcamsrc_params_destroy_list);

  while (*part) {
    gst_droidcamsrc_params_parse (params, *part);
    ++part;
  }

  g_strfreev (parts);

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

  /* ugly is the least to be said */
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GList *list = (GList *) value;
    gchar *str = NULL;
    int len = g_list_length (list);
    g_assert (len > 0);

    if (len == 1) {
      /* simple case */
      str = g_strdup_printf ("%s=%s", (gchar *) key, (gchar *) list->data);
    } else {
      int x;
      for (x = 0; x < len; x++) {
        GList *item = g_list_nth (list, x);
        if (str == NULL) {
          str = g_strdup_printf ("%s=%s", (gchar *) key, (gchar *) item->data);
        } else {
          gchar *new_str = g_strjoin (",", str, (gchar *) item->data, NULL);
          g_free (str);
          str = new_str;
        }
      }
    }

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

GstCaps *
gst_droidcamsrc_params_get_viewfinder_caps (GstDroidCamSrcParams * params)
{
  int fps;
  GstCapsFeatures *feature;
  GstCaps *caps = gst_caps_new_empty ();
  GList *item;

  g_mutex_lock (&params->lock);

  fps = gst_droidcamsrc_params_get_int_locked (params, "preview-frame-rate");
  if (fps == -1) {
    goto unlock_and_out;
  }

  item = gst_droidcamsrc_params_get_item_locked (params, "preview-size-values");
  if (!item) {
    goto unlock_and_out;
  }

  while (item) {
    int width, height;
    GstCaps *caps2;

    if (gst_droidcamsrc_params_parse_dimension (item->data, &width, &height)) {
      caps2 = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "ENCODED",
          "width", G_TYPE_INT, width,
          "height", G_TYPE_INT, height,
          "framerate", GST_TYPE_FRACTION, fps, 1, NULL);

      feature =
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DROID_HANDLE, NULL);
      gst_caps_set_features (caps2, 0, feature);

      caps = gst_caps_merge (caps, caps2);
    }

    item = g_list_next (item);
  }

unlock_and_out:
  g_mutex_unlock (&params->lock);

  return caps;
}

GstCaps *
gst_droidcamsrc_params_get_video_caps (GstDroidCamSrcParams * params)
{
  GstCaps *caps = gst_caps_new_empty ();
  GList *item;
  int fps;
  GstCapsFeatures *feature;

  g_mutex_lock (&params->lock);
  fps = gst_droidcamsrc_params_get_int_locked (params, "preview-frame-rate");
  if (fps == -1) {
    goto unlock_and_out;
  }

  item = gst_droidcamsrc_params_get_item_locked (params, "video-size-values");
  if (!item) {
    goto unlock_and_out;
  }

  while (item) {
    int width, height;
    GstCaps *caps2;

    if (gst_droidcamsrc_params_parse_dimension (item->data, &width, &height)) {
      caps2 = gst_caps_new_simple ("video/x-raw",
          "format", G_TYPE_STRING, "ENCODED",
          "width", G_TYPE_INT, width,
          "height", G_TYPE_INT, height,
          "framerate", GST_TYPE_FRACTION, fps, 1, NULL);

      feature =
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DROID_VIDEO_META_DATA,
          NULL);
      gst_caps_set_features (caps2, 0, feature);

      caps = gst_caps_merge (caps, caps2);
    }

    item = g_list_next (item);
  }

unlock_and_out:
  g_mutex_unlock (&params->lock);

  return caps;
}

GstCaps *
gst_droidcamsrc_params_get_image_caps (GstDroidCamSrcParams * params)
{
  GstCaps *caps = gst_caps_new_empty ();
  GList *item;
  int fps;

  g_mutex_lock (&params->lock);
  fps = gst_droidcamsrc_params_get_int_locked (params, "preview-frame-rate");
  if (fps == -1) {
    goto unlock_and_out;
  }

  item = gst_droidcamsrc_params_get_item_locked (params, "picture-size-values");
  if (!item) {
    goto unlock_and_out;
  }

  while (item) {
    int width, height;
    GstCaps *caps2;

    if (gst_droidcamsrc_params_parse_dimension (item->data, &width, &height)) {
      caps2 = gst_caps_new_simple ("image/jpeg",
          "width", G_TYPE_INT, width,
          "height", G_TYPE_INT, height,
          "framerate", GST_TYPE_FRACTION, fps, 1, NULL);

      caps = gst_caps_merge (caps, caps2);
    }

    item = g_list_next (item);
  }

unlock_and_out:
  g_mutex_unlock (&params->lock);

  return caps;
}

gboolean
gst_droidcamsrc_params_set_string (GstDroidCamSrcParams * params,
    const gchar * key, const gchar * value)
{
  GList *item;
  gboolean ret = FALSE;

  GST_DEBUG ("setting param %s to %s", key, value);

  g_mutex_lock (&params->lock);
  item = gst_droidcamsrc_params_get_item_locked (params, key);
  if (g_list_length (item) > 1) {
    GST_ERROR ("item %s has more than 1 value", key);
    goto out;
  }

  /* update only if not equal */
  if (strcmp (item->data, value)) {
    g_free (item->data);
    item->data = g_strdup (value);
    params->is_dirty = TRUE;
  }

  ret = TRUE;

out:
  g_mutex_unlock (&params->lock);
  return ret;
}
