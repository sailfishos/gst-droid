/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
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

#include <gst/gst.h>
#include "gstdroidcamsrcquirks.h"
#include "gstdroidcamsrc.h"
#include "gstdroidcamsrcparams.h"

/*
 * Quirks are non-standard functionality which can be enabled or disabled
 * by either setting a CameraParameters parameter or by doing a send_message()
 * with a specific command and arguments.
 * Quirks are read from $(sysconfdir)/gst-droid/gstdroidcamsrcquirks.conf
 *
 * NOTES:
 * - if type is not defined then we assume it's a property to keep backward compatibility
 * - the value of image and video does not matter. whether it's set or not is what matters.
 * - if neither image nor video is set, we assume image to keep backward compatibility
 *
 * Format of a property quirk definition:
 * [quirk-id]
 * type=property
 * prop=<property name>
 * on=<value to turn on>
 * off=<value to turn off>
 * direction=<-1 = all devices or a camera device id>
 * image=<any value to enable quirk in image mode>
 * video=<any value to enable quirk in video mode>
 *
 * Format of a command quirk definition:
 * [quirk-id]
 * type=command
 * command_enable=<command value used for enabling>
 * command_disable=<command value used for disabling>
 * arg1_enable=<value of the first argument used for enabling>
 * arg2_enable=<value of the second argument used for enabling>
 * arg1_disable=<value of the first argument used for disabling>
 * arg2_disable=<value of the second argument used for disabling>
 * direction=<-1 = all devices or a camera device id>
 * image=<any value to enable quirk in image mode>
 * video=<any value to enable quirk in video mode>
 */
#define CHECK_ERROR(e,s,p)						\
  if (e) {								\
    GST_WARNING ("failed to load %s for %s: %s", p, s, err->message);	\
    g_error_free (e);							\
    e = NULL;								\
  }

GST_DEBUG_CATEGORY_EXTERN (gst_droid_camsrc_debug);
#define GST_CAT_DEFAULT gst_droid_camsrc_debug

typedef enum _GstDRoidCamSrcQuirkType GstDRoidCamSrcQuirkType;

enum _GstDRoidCamSrcQuirkType
{
  GST_DROID_CAM_SRC_QUIRK_PROPERTY = 0,
  GST_DROID_CAM_SRC_QUIRK_COMMAND = 1,
};

struct _GstDroidCamSrcQuirks
{
  GList *quirks;
};

struct _GstDroidCamSrcQuirk
{
  gint direction;
  gchar *id;
  gboolean image;
  gboolean video;

  GstDRoidCamSrcQuirkType type;

  /* property quirk */
  gchar *prop;
  gchar *on;
  gchar *off;

  /* command quirk */
  gint command_enable;
  gint command_disable;
  gint arg1_enable;
  gint arg2_enable;
  gint arg1_disable;
  gint arg2_disable;
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
  gchar *type = NULL;

  /* common properties first */
  quirk->id = g_strdup (group);

  quirk->direction = g_key_file_get_integer (file, group, "direction", &err);
  CHECK_ERROR (err, group, "direction");

  {
    gboolean has_image = g_key_file_has_key (file, group, "image", NULL);
    gboolean has_video = g_key_file_has_key (file, group, "video", NULL);

    if (!has_image && !has_video) {
      /* backwards compatibility */
      quirk->image = TRUE;
      quirk->video = FALSE;
    } else {
      quirk->image = has_image;
      quirk->video = has_video;
    }
  }

  type = g_key_file_get_value (file, group, "type", &err);
  if (err) {
    /* CHECK_ERROR() will issue a warning if the key "type" is not defined
     * but this is valid as the key is not mandatory */
    g_error_free (err);
    err = NULL;
  }

  quirk->type = GST_DROID_CAM_SRC_QUIRK_PROPERTY;

  if (!g_strcmp0 (type, "command")) {
    quirk->type = GST_DROID_CAM_SRC_QUIRK_COMMAND;
  }

  if (type) {
    g_free (type);
    type = NULL;
  }

  if (quirk->type == GST_DROID_CAM_SRC_QUIRK_PROPERTY) {
    quirk->prop = g_key_file_get_value (file, group, "prop", &err);
    CHECK_ERROR (err, group, "prop");

    quirk->on = g_key_file_get_value (file, group, "on", &err);
    CHECK_ERROR (err, group, "on");

    quirk->off = g_key_file_get_value (file, group, "off", &err);
    CHECK_ERROR (err, group, "off");

    if (!quirk->prop || !quirk->on || !quirk->off) {
      GST_WARNING ("incomplete quirk definition for %s", group);
      gst_droidcamsrc_quirk_free (quirk);
      quirk = NULL;
    }
  } else {

    quirk->command_enable =
        g_key_file_get_integer (file, group, "command_enable", &err);
    CHECK_ERROR (err, group, "command_enable");

    quirk->command_disable =
        g_key_file_get_integer (file, group, "command_disable", &err);
    CHECK_ERROR (err, group, "command_disable");
    quirk->arg1_enable =
        g_key_file_get_integer (file, group, "arg1_enable", &err);
    CHECK_ERROR (err, group, "arg1_enable");
    quirk->arg2_enable =
        g_key_file_get_integer (file, group, "arg2_enable", &err);
    CHECK_ERROR (err, group, "arg2_enable");
    quirk->arg1_disable =
        g_key_file_get_integer (file, group, "arg1_disable", &err);
    CHECK_ERROR (err, group, "arg1_disable");
    quirk->arg2_disable =
        g_key_file_get_integer (file, group, "arg2_disable", &err);
    CHECK_ERROR (err, group, "arg2_disable");
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
  gchar **groups = NULL;
  gsize len = 0;
  int x;

  if (!g_key_file_load_from_file (file, file_path, G_KEY_FILE_NONE, &err)) {
    GST_WARNING ("failed to load configuration file %s: %s", file_path,
        err->message);
  }

  if (err) {
    g_error_free (err);
    err = NULL;
  }

  groups = g_key_file_get_groups (file, &len);
  quirks->quirks = NULL;
  for (x = 0; x < len; x++) {
    GstDroidCamSrcQuirk *quirk = gst_droidcamsrc_quirk_new (file, groups[x]);
    if (quirk) {
      GST_INFO ("parsed quirk %s", groups[x]);
      quirks->quirks = g_list_append (quirks->quirks, quirk);
    }
  }

  g_strfreev (groups);
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

void
gst_droidcamsrc_quirks_apply (GstDroidCamSrcQuirks * quirks,
    GstDroidCamSrc * src, gint direction, gint mode, const gchar * quirk_id,
    gboolean enable)
{
  const GstDroidCamSrcQuirk *quirk;

  quirk = gst_droidcamsrc_quirks_get_quirk (quirks, quirk_id);

  if (!quirk) {
    GST_INFO_OBJECT (src, "quirk %s not known", quirk_id);
    return;
  }

  gst_droidcamsrc_quirks_apply_quirk (quirks, src, direction, mode, quirk,
      enable);
}

void
gst_droidcamsrc_quirks_apply_quirk (GstDroidCamSrcQuirks * quirks,
    GstDroidCamSrc * src, gint direction, gint mode,
    const GstDroidCamSrcQuirk * quirk, gboolean enable)
{
  gboolean same_direction;
  gboolean same_mode;

  GST_INFO_OBJECT (src,
      "apply quirk %s: direction is %d, mode is %d, requested direction is %d",
      quirk->id, quirk->direction, mode, direction);

  same_direction = (quirk->direction == direction || quirk->direction == -1);
  same_mode = ((quirk->image && mode == MODE_IMAGE) || (quirk->video
          && mode == MODE_VIDEO));

  if (same_direction && same_mode && enable) {
    GST_INFO_OBJECT (src, "enabling %s", quirk->id);

    if (quirk->type == GST_DROID_CAM_SRC_QUIRK_PROPERTY) {
      gst_droidcamsrc_params_set_string (src->dev->params,
          quirk->prop, quirk->on);
    } else {
      gst_droidcamsrc_dev_send_command (src->dev, quirk->command_enable,
          quirk->arg1_enable, quirk->arg2_enable);
    }
  } else {
    GST_INFO_OBJECT (src, "disabling %s", quirk->id);
    if (quirk->type == GST_DROID_CAM_SRC_QUIRK_PROPERTY) {
      gst_droidcamsrc_params_set_string (src->dev->params,
          quirk->prop, quirk->off);
    } else {
      gst_droidcamsrc_dev_send_command (src->dev, quirk->command_disable,
          quirk->arg1_disable, quirk->arg2_disable);
    }
  }
}

G_INLINE_FUNC gint
_find_quirk (gconstpointer a, gconstpointer b)
{
  /* a can be NULL */
  return a && b ? g_strcmp0 (((GstDroidCamSrcQuirk *) a)->id, (gchar *) b) : -1;
}

const GstDroidCamSrcQuirk *
gst_droidcamsrc_quirks_get_quirk (GstDroidCamSrcQuirks * quirks,
    const gchar * id)
{
  const GList *data = g_list_find_custom (quirks->quirks, id,
      _find_quirk);

  if (data) {
    return data->data;
  }

  return NULL;
}

gboolean
gst_droidcamsrc_quirk_is_property (const GstDroidCamSrcQuirk * quirk)
{
  return quirk->type == GST_DROID_CAM_SRC_QUIRK_PROPERTY;
}
