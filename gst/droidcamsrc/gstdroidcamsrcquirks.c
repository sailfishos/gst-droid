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

#include <gst/gst.h>
#include "gstdroidcamsrcquirks.h"
#include "gstdroidcamsrc.h"
#include "gstdroidcamsrcparams.h"

#define CHECK_ERROR(e,s,p)						\
  if (e) {								\
    GST_WARNING ("failed to load %s for %s: %s", p, s, err->message);	\
    g_error_free (e);							\
    e = NULL;								\
  }

GST_DEBUG_CATEGORY_EXTERN (gst_droid_camsrc_debug);
#define GST_CAT_DEFAULT gst_droid_camsrc_debug

struct _GstDroidCamSrcQuirks
{
  GList *quirks;
};

struct _GstDroidCamSrcQuirk
{
  gint direction;
  gchar *id;
  gchar *prop;
  gchar *on;
  gchar *off;
};

void
gst_droidcamsrc_quirk_free (GstDroidCamSrcQuirk * quirk)
{
  if (!quirk) {
    return;
  }

  if (quirk->prop) {
    g_free (quirk->prop);
  }

  if (quirk->on) {
    g_free (quirk->on);
  }

  if (quirk->off) {
    g_free (quirk->off);
  }

  if (quirk->id) {
    g_free (quirk->id);
  }

  g_slice_free (GstDroidCamSrcQuirk, quirk);
}

GstDroidCamSrcQuirk *
gst_droidcamsrc_quirk_new (GKeyFile * file, const gchar * group)
{
  GstDroidCamSrcQuirk *quirk = g_slice_new0 (GstDroidCamSrcQuirk);
  GError *err = NULL;

  quirk->id = g_strdup (group);

  quirk->prop = g_key_file_get_value (file, group, "prop", &err);
  CHECK_ERROR (err, group, "prop");

  quirk->on = g_key_file_get_value (file, group, "on", &err);
  CHECK_ERROR (err, group, "on");

  quirk->off = g_key_file_get_value (file, group, "off", &err);
  CHECK_ERROR (err, group, "off");

  quirk->direction = g_key_file_get_integer (file, group, "direction", &err);
  CHECK_ERROR (err, group, "direction");

  if (!quirk->prop || !quirk->on || !quirk->off) {
    GST_WARNING ("incomplete quirk definition for %s", group);
    gst_droidcamsrc_quirk_free (quirk);
    quirk = NULL;
  }

  return quirk;
}

GstDroidCamSrcQuirks *
gst_droidcamsrc_quirks_new ()
{
  GKeyFile *file = g_key_file_new ();
  gchar *file_path =
      g_build_path ("/", SYSCONFDIR, "gst-droid/gstdroidcamsrcquirks.conf",
      NULL);
  GError *err = NULL;
  GstDroidCamSrcQuirks *quirks = g_slice_new0 (GstDroidCamSrcQuirks);

  if (!g_key_file_load_from_file (file, file_path, G_KEY_FILE_NONE, &err)) {
    GST_WARNING ("failed to load configuration file %s: %s", file_path,
        err->message);
  }

  if (err) {
    g_error_free (err);
    err = NULL;
  }

  quirks->quirks = NULL;
  quirks->quirks =
      g_list_append (quirks->quirks, gst_droidcamsrc_quirk_new (file,
          "face-detection"));
  quirks->quirks =
      g_list_append (quirks->quirks, gst_droidcamsrc_quirk_new (file,
          "image-noise-reduction"));

  g_free (file_path);
  g_key_file_unref (file);

  return quirks;
}

void
gst_droidcamsrc_quirks_destroy (GstDroidCamSrcQuirks * quirks)
{
  g_list_free_full (quirks->quirks,
      (GDestroyNotify) gst_droidcamsrc_quirk_free);
  g_slice_free (GstDroidCamSrcQuirks, quirks);
}

static gint
_find_quirk (gconstpointer a, gconstpointer b)
{
  return g_strcmp0 (((GstDroidCamSrcQuirk *) a)->id, (gchar *) b);
}

void
gst_droidcamsrc_quirks_apply (GstDroidCamSrcQuirks * quirks,
    GstDroidCamSrc * src, gint direction, const gchar * quirk_id,
    gboolean enable)
{
  GstDroidCamSrcQuirk *quirk =
      (GstDroidCamSrcQuirk *) (g_list_find_custom (quirks->quirks, quirk_id,
          _find_quirk)->data);

  if (!quirk) {
    GST_DEBUG_OBJECT (src, "quirk %s not known", quirk_id);
    return;
  }

  GST_INFO_OBJECT (src,
      "quirk %s direction is %d and requested direction is %d", quirk_id,
      quirk->direction, direction);

  if (quirk->direction == direction || quirk->direction == -1) {
    if (enable) {
      GST_DEBUG_OBJECT (src, "enabling %s", quirk_id);
      gst_droidcamsrc_params_set_string (src->dev->params,
          quirk->prop, quirk->on);
    } else {
      GST_DEBUG_OBJECT (src, "disabling %s", quirk_id);
      gst_droidcamsrc_params_set_string (src->dev->params,
          quirk->prop, quirk->off);
    }
  }
}
