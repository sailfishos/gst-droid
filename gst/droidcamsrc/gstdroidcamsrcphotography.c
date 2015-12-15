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

#include "gstdroidcamsrcphotography.h"
#include "gstdroidcamsrc.h"
#include <stdlib.h>             /* atoi() */
#include <string.h>             /* memcpy() */
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include <gst/interfaces/photography.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droid_camsrc_debug);
#define GST_CAT_DEFAULT gst_droid_camsrc_debug

struct Entry
{
  int prop;
  const gchar *photo_prop;
};

struct Entry Entries[] = {
  {PROP_WB_MODE, GST_PHOTOGRAPHY_PROP_WB_MODE},
  {PROP_COLOR_TONE, GST_PHOTOGRAPHY_PROP_COLOR_TONE},
  {PROP_SCENE_MODE, GST_PHOTOGRAPHY_PROP_SCENE_MODE},
  {PROP_FLASH_MODE, GST_PHOTOGRAPHY_PROP_FLASH_MODE},
  {PROP_FLICKER_MODE, GST_PHOTOGRAPHY_PROP_FLICKER_MODE},
  {PROP_FOCUS_MODE, GST_PHOTOGRAPHY_PROP_FOCUS_MODE},
  {PROP_EXPOSURE_MODE, GST_PHOTOGRAPHY_PROP_EXPOSURE_MODE},
  {PROP_NOISE_REDUCTION, GST_PHOTOGRAPHY_PROP_NOISE_REDUCTION},
  {PROP_ZOOM, GST_PHOTOGRAPHY_PROP_ZOOM},
  {PROP_EV_COMP, GST_PHOTOGRAPHY_PROP_EV_COMP},
  {PROP_ANALOG_GAIN, GST_PHOTOGRAPHY_PROP_ANALOG_GAIN},
  {PROP_LENS_FOCUS, GST_PHOTOGRAPHY_PROP_LENS_FOCUS},
  {PROP_APERTURE, GST_PHOTOGRAPHY_PROP_APERTURE},
  {PROP_ISO_SPEED, GST_PHOTOGRAPHY_PROP_ISO_SPEED},
  {PROP_COLOR_TEMPERATURE, GST_PHOTOGRAPHY_PROP_COLOR_TEMPERATURE},
  {PROP_MIN_EXPOSURE_TIME, GST_PHOTOGRAPHY_PROP_MIN_EXPOSURE_TIME},
  {PROP_MAX_EXPOSURE_TIME, GST_PHOTOGRAPHY_PROP_MAX_EXPOSURE_TIME},
  {PROP_EXPOSURE_TIME, GST_PHOTOGRAPHY_PROP_EXPOSURE_TIME},
  {PROP_CAPABILITIES, GST_PHOTOGRAPHY_PROP_CAPABILITIES},
  {PROP_IMAGE_CAPTURE_SUPPORTED_CAPS,
      GST_PHOTOGRAPHY_PROP_IMAGE_CAPTURE_SUPPORTED_CAPS},
  {PROP_IMAGE_PREVIEW_SUPPORTED_CAPS,
      GST_PHOTOGRAPHY_PROP_IMAGE_PREVIEW_SUPPORTED_CAPS},
  {PROP_WHITE_POINT, GST_PHOTOGRAPHY_PROP_WHITE_POINT},
};

struct _GstDroidCamSrcPhotography
{
  GstPhotographySettings settings;
  GList *flash;
  GList *color_tone;
  GList *focus;
  GList *scene;
  GList *wb;
  GList *iso;
  GList *flicker;
};

struct DataEntry
{
  int key;
  gchar *value;
};

static GList *gst_droidcamsrc_photography_load (GKeyFile * file,
    const gchar * property);

#define PHOTO_IFACE_FUNC(name, tset, tget)					\
  static gboolean gst_droidcamsrc_get_##name (GstDroidCamSrc * src, tget val); \
  static gboolean gst_droidcamsrc_set_##name (GstDroidCamSrc * src, tset val); \
  static gboolean gst_droidcamsrc_photography_get_##name (GstPhotography *photo, tget val) \
  { \
    return gst_droidcamsrc_get_##name (GST_DROIDCAMSRC (photo), val);	\
  }                                                                                             \
  static gboolean gst_droidcamsrc_photography_set_##name (GstPhotography *photo, tset val) \
  { \
    return gst_droidcamsrc_set_##name (GST_DROIDCAMSRC (photo), val);	\
  }

#define APPLY_SETTING(table, val, droid)				\
  {									\
    int x;								\
    int len = g_list_length (table);					\
    gchar *value = NULL;						\
    for (x = 0; x < len; x++) {						\
      struct DataEntry *entry = (struct DataEntry *) g_list_nth_data (table, x); \
      if (val == entry->key) {						\
	value = entry->value;						\
	break;								\
      }									\
    }									\
									\
    if (value) {							\
      GST_INFO_OBJECT (src, "setting %s to %s", droid, value);		\
      gst_droidcamsrc_params_set_string (src->dev->params, droid, value); \
    } else {								\
      GST_WARNING_OBJECT (src, "setting %s to %d is not supported", droid, val); \
    }									\
  }

#define SET_ENUM(table,val,droid,memb)					\
  int x;								\
  int len = g_list_length (table);					\
  const gchar *value = NULL;						\
  for (x = 0; x < len; x++) {						\
    struct DataEntry *entry = (struct DataEntry *) g_list_nth_data (table, x); \
    if (val == entry->key) {						\
      value = entry->value;						\
      break;								\
    }									\
  }									\
									\
  if (!value) {								\
    GST_WARNING_OBJECT (src, "setting %s to %d is not supported", droid, val); \
    return FALSE;							\
  }									\
									\
  GST_DEBUG_OBJECT (src, "setting %s to %s (%d)", droid, value, val);	\
									\
  GST_OBJECT_LOCK (src);						\
  src->photo->settings.memb = val;					\
  GST_OBJECT_UNLOCK (src);						\
  return gst_droidcamsrc_set_and_apply (src, droid, value);

PHOTO_IFACE_FUNC (ev_compensation, float, float *);
PHOTO_IFACE_FUNC (iso_speed, guint, guint *);
PHOTO_IFACE_FUNC (aperture, guint, guint *);
PHOTO_IFACE_FUNC (exposure, guint32, guint32 *);
PHOTO_IFACE_FUNC (white_balance_mode, GstPhotographyWhiteBalanceMode,
    GstPhotographyWhiteBalanceMode *);
PHOTO_IFACE_FUNC (color_tone_mode, GstPhotographyColorToneMode,
    GstPhotographyColorToneMode *);
PHOTO_IFACE_FUNC (scene_mode, GstPhotographySceneMode,
    GstPhotographySceneMode *);
PHOTO_IFACE_FUNC (flash_mode, GstPhotographyFlashMode,
    GstPhotographyFlashMode *);
PHOTO_IFACE_FUNC (zoom, gfloat, gfloat *);
PHOTO_IFACE_FUNC (flicker_mode, GstPhotographyFlickerReductionMode,
    GstPhotographyFlickerReductionMode *);
PHOTO_IFACE_FUNC (focus_mode, GstPhotographyFocusMode,
    GstPhotographyFocusMode *);
PHOTO_IFACE_FUNC (noise_reduction, GstPhotographyNoiseReduction,
    GstPhotographyNoiseReduction *);
PHOTO_IFACE_FUNC (config, GstPhotographySettings *, GstPhotographySettings *);
PHOTO_IFACE_FUNC (exposure_mode, GstPhotographyExposureMode,
    GstPhotographyExposureMode *);
PHOTO_IFACE_FUNC (analog_gain, gfloat, gfloat *);
PHOTO_IFACE_FUNC (lens_focus, gfloat, gfloat *);
PHOTO_IFACE_FUNC (color_temperature, guint, guint *);
PHOTO_IFACE_FUNC (min_exposure_time, guint, guint *);
PHOTO_IFACE_FUNC (max_exposure_time, guint, guint *);

static GstPhotographyCaps gst_droidcamsrc_get_capabilities (GstDroidCamSrc *
    src);
static gboolean gst_droidcamsrc_set_and_apply (GstDroidCamSrc * src,
    const gchar * key, const gchar * value);
static gboolean gst_droidcamsrc_prepare_for_capture (GstDroidCamSrc * src,
    GstPhotographyCapturePrepared func,
    GstCaps * capture_caps, gpointer user_data);
static void gst_droidcamsrc_set_autofocus (GstDroidCamSrc * src, gboolean on);
static void gst_droidcamsrc_photography_set_iso_to_droid (GstDroidCamSrc * src);
static void gst_droidcamsrc_photography_set_zoom_to_droid (GstDroidCamSrc *
    src);
static void
gst_droidcamsrc_photography_set_ev_compensation_to_droid (GstDroidCamSrc * src);

static void
free_data_entry (gpointer * data)
{
  struct DataEntry *entry = (struct DataEntry *) data;
  g_free (entry->value);
  g_slice_free (struct DataEntry, entry);
}

static GstPhotographyCaps
gst_droidcamsrc_photography_get_capabilities (GstPhotography * photo)
{
  return gst_droidcamsrc_get_capabilities (GST_DROIDCAMSRC (photo));
}

static gboolean
gst_droidcamsrc_photography_prepare_for_capture (GstPhotography * photo,
    GstPhotographyCapturePrepared func,
    GstCaps * capture_caps, gpointer user_data)
{
  return gst_droidcamsrc_prepare_for_capture (GST_DROIDCAMSRC (photo), func,
      capture_caps, user_data);
}

static void
gst_droidcamsrc_photography_set_autofocus (GstPhotography * photo, gboolean on)
{
  gst_droidcamsrc_set_autofocus (GST_DROIDCAMSRC (photo), on);
}

void
gst_droidcamsrc_photography_register (G_GNUC_UNUSED gpointer g_iface,
    gpointer iface_data)
{
  GstPhotographyInterface *iface = (GstPhotographyInterface *) g_iface;
  iface->get_ev_compensation = gst_droidcamsrc_photography_get_ev_compensation;
  iface->get_iso_speed = gst_droidcamsrc_photography_get_iso_speed;
  iface->get_aperture = gst_droidcamsrc_photography_get_aperture;
  iface->get_exposure = gst_droidcamsrc_photography_get_exposure;
  iface->get_white_balance_mode =
      gst_droidcamsrc_photography_get_white_balance_mode;
  iface->get_color_tone_mode = gst_droidcamsrc_photography_get_color_tone_mode;
  iface->get_scene_mode = gst_droidcamsrc_photography_get_scene_mode;
  iface->get_flash_mode = gst_droidcamsrc_photography_get_flash_mode;
  iface->get_zoom = gst_droidcamsrc_photography_get_zoom;
  iface->get_flicker_mode = gst_droidcamsrc_photography_get_flicker_mode;
  iface->get_focus_mode = gst_droidcamsrc_photography_get_focus_mode;
  iface->set_ev_compensation = gst_droidcamsrc_photography_set_ev_compensation;
  iface->set_iso_speed = gst_droidcamsrc_photography_set_iso_speed;
  iface->set_aperture = gst_droidcamsrc_photography_set_aperture;
  iface->set_exposure = gst_droidcamsrc_photography_set_exposure;
  iface->set_white_balance_mode =
      gst_droidcamsrc_photography_set_white_balance_mode;
  iface->set_color_tone_mode = gst_droidcamsrc_photography_set_color_tone_mode;
  iface->set_scene_mode = gst_droidcamsrc_photography_set_scene_mode;
  iface->set_flash_mode = gst_droidcamsrc_photography_set_flash_mode;
  iface->set_zoom = gst_droidcamsrc_photography_set_zoom;
  iface->set_flicker_mode = gst_droidcamsrc_photography_set_flicker_mode;
  iface->set_focus_mode = gst_droidcamsrc_photography_set_focus_mode;
  iface->get_capabilities = gst_droidcamsrc_photography_get_capabilities;
  iface->prepare_for_capture = gst_droidcamsrc_photography_prepare_for_capture;
  iface->set_autofocus = gst_droidcamsrc_photography_set_autofocus;
  iface->set_config = gst_droidcamsrc_photography_set_config;
  iface->get_config = gst_droidcamsrc_photography_get_config;
  iface->get_noise_reduction = gst_droidcamsrc_photography_get_noise_reduction;
  iface->set_noise_reduction = gst_droidcamsrc_photography_set_noise_reduction;
  iface->get_exposure_mode = gst_droidcamsrc_photography_get_exposure_mode;
  iface->set_exposure_mode = gst_droidcamsrc_photography_set_exposure_mode;
  iface->get_analog_gain = gst_droidcamsrc_photography_get_analog_gain;
  iface->set_analog_gain = gst_droidcamsrc_photography_set_analog_gain;
  iface->get_lens_focus = gst_droidcamsrc_photography_get_lens_focus;
  iface->set_lens_focus = gst_droidcamsrc_photography_set_lens_focus;
  iface->get_color_temperature =
      gst_droidcamsrc_photography_get_color_temperature;
  iface->set_color_temperature =
      gst_droidcamsrc_photography_set_color_temperature;
  iface->get_min_exposure_time =
      gst_droidcamsrc_photography_get_min_exposure_time;
  iface->set_min_exposure_time =
      gst_droidcamsrc_photography_set_min_exposure_time;
  iface->get_max_exposure_time =
      gst_droidcamsrc_photography_get_max_exposure_time;
  iface->set_max_exposure_time =
      gst_droidcamsrc_photography_set_max_exposure_time;
}

void
gst_droidcamsrc_photography_add_overrides (GObjectClass * klass)
{
  int x;
  int len = G_N_ELEMENTS (Entries);

  for (x = 0; x < len; x++) {
    g_object_class_override_property (klass, Entries[x].prop,
        Entries[x].photo_prop);
  }
}

gboolean
gst_droidcamsrc_photography_get_property (GstDroidCamSrc * src, guint prop_id,
    GValue * value, G_GNUC_UNUSED GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_WB_MODE:
    {
      GstPhotographyWhiteBalanceMode mode;
      if (gst_droidcamsrc_get_white_balance_mode (src, &mode)) {
        g_value_set_enum (value, mode);
      }
    }
      return TRUE;

    case PROP_COLOR_TONE:
    {
      GstPhotographyColorToneMode tone;
      if (gst_droidcamsrc_get_color_tone_mode (src, &tone)) {
        g_value_set_enum (value, tone);
      }
    }
      return TRUE;

    case PROP_SCENE_MODE:
    {
      GstPhotographySceneMode mode;
      if (gst_droidcamsrc_get_scene_mode (src, &mode)) {
        g_value_set_enum (value, mode);
      }
    }
      return TRUE;

    case PROP_FLASH_MODE:
    {
      GstPhotographyFlashMode mode;
      if (gst_droidcamsrc_get_flash_mode (src, &mode)) {
        g_value_set_enum (value, mode);
      }
    }
      return TRUE;

    case PROP_FLICKER_MODE:
    {
      GstPhotographyFlickerReductionMode mode;
      if (gst_droidcamsrc_get_flicker_mode (src, &mode)) {
        g_value_set_enum (value, mode);
      }
    }
      return TRUE;

    case PROP_FOCUS_MODE:
    {
      GstPhotographyFocusMode mode;
      if (gst_droidcamsrc_get_focus_mode (src, &mode)) {
        g_value_set_enum (value, mode);
      }
    }
      return TRUE;

    case PROP_NOISE_REDUCTION:
    {
      GstPhotographyNoiseReduction mode;
      if (gst_droidcamsrc_get_noise_reduction (src, &mode)) {
        g_value_set_enum (value, mode);
      }
    }
      return TRUE;

    case PROP_EXPOSURE_MODE:
    {
      GstPhotographyExposureMode mode;
      if (gst_droidcamsrc_get_exposure_mode (src, &mode)) {
        g_value_set_enum (value, mode);
      }
    }
      return TRUE;

    case PROP_ZOOM:
    {
      gfloat zoom;
      if (gst_droidcamsrc_get_zoom (src, &zoom)) {
        g_value_set_float (value, zoom);
      }
    }
      return TRUE;

    case PROP_EV_COMP:
    {
      gfloat ev;
      if (gst_droidcamsrc_get_ev_compensation (src, &ev)) {
        g_value_set_float (value, ev);
      }
    }
      return TRUE;

    case PROP_ANALOG_GAIN:
    {
      gfloat gain;
      if (gst_droidcamsrc_get_analog_gain (src, &gain)) {
        g_value_set_float (value, gain);
      }
    }
      return TRUE;

    case PROP_LENS_FOCUS:
    {
      gfloat focus;
      if (gst_droidcamsrc_get_lens_focus (src, &focus)) {
        g_value_set_float (value, focus);
      }
    }
      return TRUE;

    case PROP_APERTURE:
    {
      guint aperture;
      if (gst_droidcamsrc_get_aperture (src, &aperture)) {
        g_value_set_uint (value, aperture);
      }
    }
      return TRUE;

    case PROP_ISO_SPEED:
    {
      guint iso;
      if (gst_droidcamsrc_get_iso_speed (src, &iso)) {
        g_value_set_uint (value, iso);
      }
    }
      return TRUE;

    case PROP_COLOR_TEMPERATURE:
    {
      guint color;
      if (gst_droidcamsrc_get_color_temperature (src, &color)) {
        g_value_set_uint (value, color);
      }
    }
      return TRUE;

    case PROP_MIN_EXPOSURE_TIME:
    {
      guint time;
      if (gst_droidcamsrc_get_min_exposure_time (src, &time)) {
        g_value_set_uint (value, time);
      }
    }
      return TRUE;

    case PROP_MAX_EXPOSURE_TIME:
    {
      guint time;
      if (gst_droidcamsrc_get_max_exposure_time (src, &time)) {
        g_value_set_uint (value, time);
      }
    }
      return TRUE;

    case PROP_EXPOSURE_TIME:
    {
      guint32 exposure;
      if (gst_droidcamsrc_get_exposure (src, &exposure)) {
        g_value_set_uint (value, exposure);
      }
    }
      return TRUE;

    case PROP_CAPABILITIES:
    {
      gulong capabilities = gst_droidcamsrc_get_capabilities (src);
      g_value_set_ulong (value, capabilities);
    }
      return TRUE;

    case PROP_IMAGE_CAPTURE_SUPPORTED_CAPS:
    {
      GstCaps *caps = NULL;

      g_rec_mutex_lock (&src->dev_lock);

      if (src->dev && src->dev->params) {
        caps = gst_droidcamsrc_params_get_image_caps (src->dev->params);
      }

      g_rec_mutex_unlock (&src->dev_lock);

      if (!caps) {
        caps = gst_pad_get_pad_template_caps (src->imgsrc->pad);
      }

      gst_value_set_caps (value, caps);
      gst_caps_unref (caps);
    }

      return TRUE;

    case PROP_IMAGE_PREVIEW_SUPPORTED_CAPS:
    {
      // TODO:
    }
      return TRUE;

    case PROP_WHITE_POINT:
    {
      /* not supported */
    }
      return TRUE;
  }

  return FALSE;
}

gboolean
gst_droidcamsrc_photography_set_property (GstDroidCamSrc * src, guint prop_id,
    const GValue * value, G_GNUC_UNUSED GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_WB_MODE:
      gst_droidcamsrc_set_white_balance_mode (src, g_value_get_enum (value));
      return TRUE;

    case PROP_COLOR_TONE:
      gst_droidcamsrc_set_color_tone_mode (src, g_value_get_enum (value));
      return TRUE;

    case PROP_SCENE_MODE:
      gst_droidcamsrc_set_scene_mode (src, g_value_get_enum (value));
      return TRUE;

    case PROP_FLASH_MODE:
      gst_droidcamsrc_set_flash_mode (src, g_value_get_enum (value));
      return TRUE;

    case PROP_FLICKER_MODE:
      gst_droidcamsrc_set_flicker_mode (src, g_value_get_enum (value));
      return TRUE;

    case PROP_FOCUS_MODE:
      gst_droidcamsrc_set_focus_mode (src, g_value_get_enum (value));
      return TRUE;

    case PROP_NOISE_REDUCTION:
      gst_droidcamsrc_set_noise_reduction (src, g_value_get_enum (value));
      return TRUE;

    case PROP_EXPOSURE_MODE:
      gst_droidcamsrc_set_exposure_mode (src, g_value_get_enum (value));
      return TRUE;

    case PROP_ZOOM:
      gst_droidcamsrc_set_zoom (src, g_value_get_float (value));
      return TRUE;

    case PROP_EV_COMP:
      gst_droidcamsrc_set_ev_compensation (src, g_value_get_float (value));
      return TRUE;

    case PROP_ANALOG_GAIN:
      gst_droidcamsrc_set_analog_gain (src, g_value_get_float (value));
      return TRUE;

    case PROP_LENS_FOCUS:
      gst_droidcamsrc_set_lens_focus (src, g_value_get_float (value));
      return TRUE;

    case PROP_APERTURE:
      gst_droidcamsrc_set_aperture (src, g_value_get_uint (value));
      return TRUE;

    case PROP_ISO_SPEED:
      gst_droidcamsrc_set_iso_speed (src, g_value_get_uint (value));
      return TRUE;

    case PROP_COLOR_TEMPERATURE:
      gst_droidcamsrc_set_color_temperature (src, g_value_get_uint (value));
      return TRUE;

    case PROP_MIN_EXPOSURE_TIME:
      gst_droidcamsrc_set_min_exposure_time (src, g_value_get_uint (value));
      return TRUE;

    case PROP_MAX_EXPOSURE_TIME:
      gst_droidcamsrc_set_max_exposure_time (src, g_value_get_uint (value));
      return TRUE;

    case PROP_EXPOSURE_TIME:
      gst_droidcamsrc_set_exposure (src, g_value_get_uint (value));
      return TRUE;

    case PROP_WHITE_POINT:
      /* not supported */
      return TRUE;
  }

  return FALSE;
}

static gint
sort_desc (gconstpointer a, gconstpointer b)
{
  struct DataEntry *_a = (struct DataEntry *) a;
  struct DataEntry *_b = (struct DataEntry *) b;

  if (_a->key == _b->key) {
    return 0;
  }

  if (_b->key > _a->key) {
    return 1;
  }

  return -1;
}

void
gst_droidcamsrc_photography_init (GstDroidCamSrc * src, gint dev)
{
  int x;
  GKeyFile *file = g_key_file_new ();
  gchar *file_path =
      g_strdup_printf ("/%s/gst-droid/gstdroidcamsrc-%d.conf", SYSCONFDIR, dev);
  GError *err = NULL;

  GST_INFO_OBJECT (src, "using configuration file %s", file_path);

  if (!src->photo) {
    src->photo = g_slice_new0 (GstDroidCamSrcPhotography);
    src->photo->settings.wb_mode = GST_PHOTOGRAPHY_WB_MODE_AUTO;
    src->photo->settings.tone_mode = GST_PHOTOGRAPHY_COLOR_TONE_MODE_NORMAL;
    src->photo->settings.scene_mode = GST_PHOTOGRAPHY_SCENE_MODE_AUTO;
    src->photo->settings.flash_mode = GST_PHOTOGRAPHY_FLASH_MODE_AUTO;
    src->photo->settings.ev_compensation = 0.0;
    src->photo->settings.iso_speed = 0;
    src->photo->settings.zoom = 1.0;
    src->photo->settings.flicker_mode = GST_PHOTOGRAPHY_FLICKER_REDUCTION_AUTO;
    src->photo->settings.focus_mode =
        GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL;

    /* not supported */
    src->photo->settings.aperture = 0;
    src->photo->settings.noise_reduction = 0;
    src->photo->settings.lens_focus = 0.0;
    src->photo->settings.analog_gain = 0.0;
    src->photo->settings.min_exposure_time = 0;
    src->photo->settings.max_exposure_time = 0;
    src->photo->settings.color_temperature = 0;
    src->photo->settings.exposure_time = 0;
    src->photo->settings.exposure_mode = GST_PHOTOGRAPHY_EXPOSURE_MODE_AUTO;
    for (x = 0; x < MAX_WHITE_POINT_VALUES; x++) {
      src->photo->settings.white_point[x] = 0;
    }
  }

  if (!g_key_file_load_from_file (file, file_path, G_KEY_FILE_NONE, &err)) {
    GST_WARNING ("failed to load configuration file %s: %s", file_path,
        err->message);
  }

  if (err) {
    g_error_free (err);
    err = NULL;
  }

  /* load settings */
  if (src->photo->flash) {
    g_list_free_full (src->photo->flash, (GDestroyNotify) free_data_entry);
  }
  src->photo->flash = gst_droidcamsrc_photography_load (file, "flash-mode");

  if (src->photo->color_tone) {
    g_list_free_full (src->photo->color_tone, (GDestroyNotify) free_data_entry);
  }
  src->photo->color_tone =
      gst_droidcamsrc_photography_load (file, "color-tone-mode");

  if (src->photo->focus) {
    g_list_free_full (src->photo->focus, (GDestroyNotify) free_data_entry);
  }
  src->photo->focus = gst_droidcamsrc_photography_load (file, "focus-mode");

  if (src->photo->scene) {
    g_list_free_full (src->photo->scene, (GDestroyNotify) free_data_entry);
  }
  src->photo->scene = gst_droidcamsrc_photography_load (file, "scene-mode");

  if (src->photo->wb) {
    g_list_free_full (src->photo->wb, (GDestroyNotify) free_data_entry);
  }
  src->photo->wb =
      gst_droidcamsrc_photography_load (file, "white-balance-mode");

  if (src->photo->iso) {
    g_list_free_full (src->photo->iso, (GDestroyNotify) free_data_entry);
  }
  src->photo->iso = gst_droidcamsrc_photography_load (file, "iso-speed");
  src->photo->iso = g_list_sort (src->photo->iso, sort_desc);

  if (src->photo->flicker) {
    g_list_free_full (src->photo->flicker, (GDestroyNotify) free_data_entry);
  }
  src->photo->flicker = gst_droidcamsrc_photography_load (file, "flicker-mode");

  /* free our stuff */
  g_free (file_path);
  g_key_file_unref (file);
}

void
gst_droidcamsrc_photography_destroy (GstDroidCamSrc * src)
{
  if (src->photo->flash) {
    g_list_free_full (src->photo->flash, (GDestroyNotify) free_data_entry);
    src->photo->flash = NULL;
  }

  if (src->photo->color_tone) {
    g_list_free_full (src->photo->color_tone, (GDestroyNotify) free_data_entry);
    src->photo->color_tone = NULL;
  }

  if (src->photo->focus) {
    g_list_free_full (src->photo->focus, (GDestroyNotify) free_data_entry);
    src->photo->focus = NULL;
  }

  if (src->photo->scene) {
    g_list_free_full (src->photo->scene, (GDestroyNotify) free_data_entry);
    src->photo->scene = NULL;
  }

  if (src->photo->wb) {
    g_list_free_full (src->photo->wb, (GDestroyNotify) free_data_entry);
    src->photo->wb = NULL;
  }

  if (src->photo->iso) {
    g_list_free_full (src->photo->iso, (GDestroyNotify) free_data_entry);
    src->photo->iso = NULL;
  }

  if (src->photo->flicker) {
    g_list_free_full (src->photo->flicker, (GDestroyNotify) free_data_entry);
    src->photo->flicker = NULL;
  }

  g_slice_free (GstDroidCamSrcPhotography, src->photo);
  src->photo = NULL;
}

void
gst_droidcamsrc_photography_apply (GstDroidCamSrc * src,
    GstDroidCamSrcApplyType type)
{
  GST_OBJECT_LOCK (src);
  /* properties which are not here because we don't support them:
   * aperture
   * noise reduction
   * lens focus
   * analog gain
   * color temperature
   * white point
   * min exposure time
   * max exposure time
   * exposure
   * exposure mode
   */
  gst_droidcamsrc_photography_set_flash_to_droid (src);
  gst_droidcamsrc_photography_set_focus_to_droid (src);
  gst_droidcamsrc_photography_set_iso_to_droid (src);
  gst_droidcamsrc_photography_set_zoom_to_droid (src);
  gst_droidcamsrc_photography_set_ev_compensation_to_droid (src);

  APPLY_SETTING (src->photo->wb, src->photo->settings.wb_mode, "whitebalance");
  APPLY_SETTING (src->photo->scene, src->photo->settings.scene_mode,
      "scene-mode");
  APPLY_SETTING (src->photo->color_tone, src->photo->settings.tone_mode,
      "effect");
  APPLY_SETTING (src->photo->flicker, src->photo->settings.flicker_mode,
      "antibanding");

  GST_OBJECT_UNLOCK (src);

  if (type == SET_AND_APPLY) {
    gst_droidcamsrc_apply_params (src);
  }
}

static GList *
gst_droidcamsrc_photography_load (GKeyFile * file, const gchar * property)
{
  gchar **keys;
  int x;
  GError *err = NULL;
  gsize len = 0;
  GList *list = NULL;

  keys = g_key_file_get_keys (file, property, &len, &err);

  if (err) {
    GST_WARNING ("failed to load %s: %s", property, err->message);
    g_error_free (err);
    err = NULL;
  }

  for (x = 0; x < len; x++) {
    gchar *value = g_key_file_get_value (file, property, keys[x], &err);

    if (err) {
      GST_WARNING ("failed to load %s (%s): %s", property, keys[x],
          err->message);
      g_error_free (err);
      err = NULL;
    }

    if (value) {
      int key = atoi (keys[x]);
      struct DataEntry *entry = g_slice_new (struct DataEntry);
      entry->key = key;
      entry->value = value;
      list = g_list_append (list, entry);
    }
  }

  return list;
}

static gboolean
gst_droidcamsrc_get_ev_compensation (GstDroidCamSrc * src, gfloat * ev_comp)
{
  GST_OBJECT_LOCK (src);
  *ev_comp = src->photo->settings.ev_compensation;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_droidcamsrc_get_iso_speed (GstDroidCamSrc * src, guint * iso_speed)
{
  GST_OBJECT_LOCK (src);
  *iso_speed = src->photo->settings.iso_speed;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_droidcamsrc_get_aperture (GstDroidCamSrc * src, guint * aperture)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_get_exposure (GstDroidCamSrc * src, guint32 * exposure)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_get_white_balance_mode (GstDroidCamSrc *
    src, GstPhotographyWhiteBalanceMode * wb_mode)
{
  GST_OBJECT_LOCK (src);
  *wb_mode = src->photo->settings.wb_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_droidcamsrc_get_color_tone_mode (GstDroidCamSrc *
    src, GstPhotographyColorToneMode * tone_mode)
{
  GST_OBJECT_LOCK (src);
  *tone_mode = src->photo->settings.tone_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_droidcamsrc_get_scene_mode (GstDroidCamSrc
    * src, GstPhotographySceneMode * scene_mode)
{
  GST_OBJECT_LOCK (src);
  *scene_mode = src->photo->settings.scene_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_droidcamsrc_get_flash_mode (GstDroidCamSrc
    * src, GstPhotographyFlashMode * flash_mode)
{
  GST_OBJECT_LOCK (src);
  *flash_mode = src->photo->settings.flash_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_droidcamsrc_get_zoom (GstDroidCamSrc * src, gfloat * zoom)
{
  GST_OBJECT_LOCK (src);
  *zoom = src->photo->settings.zoom;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_droidcamsrc_get_flicker_mode (GstDroidCamSrc * src,
    GstPhotographyFlickerReductionMode * flicker_mode)
{
  GST_OBJECT_LOCK (src);
  *flicker_mode = src->photo->settings.flicker_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_droidcamsrc_get_focus_mode (GstDroidCamSrc
    * src, GstPhotographyFocusMode * focus_mode)
{
  GST_OBJECT_LOCK (src);
  *focus_mode = src->photo->settings.focus_mode;
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_droidcamsrc_get_noise_reduction (GstDroidCamSrc *
    src, GstPhotographyNoiseReduction * noise_reduction)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_get_exposure_mode (GstDroidCamSrc *
    src, GstPhotographyExposureMode * exposure_mode)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_get_analog_gain (GstDroidCamSrc * src, gfloat * analog_gain)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_get_lens_focus (GstDroidCamSrc * src, gfloat * lens_focus)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_get_color_temperature (GstDroidCamSrc * src,
    guint * color_temperature)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_get_min_exposure_time (GstDroidCamSrc * src,
    guint * min_exposure_time)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_get_max_exposure_time (GstDroidCamSrc * src,
    guint * max_exposure_time)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_set_ev_compensation (GstDroidCamSrc * src, gfloat ev_comp)
{
  int val;
  gchar *value;
  gboolean ret;

  ev_comp = CLAMP (ev_comp, src->min_ev_compensation, src->max_ev_compensation);

  if (src->ev_step == 0) {
    GST_DEBUG_OBJECT (src,
        "ev_step is still unknown. discarding requested ev compensation");
    GST_OBJECT_LOCK (src);
    src->photo->settings.ev_compensation = ev_comp;
    GST_OBJECT_UNLOCK (src);
    return FALSE;
  }

  val = ev_comp / src->ev_step;

  value = g_strdup_printf ("%d", val);

  GST_INFO_OBJECT (src, "setting exposure-compensation to %s", value);

  ret = gst_droidcamsrc_set_and_apply (src, "exposure-compensation", value);

  g_free (value);

  if (ret) {
    GST_OBJECT_LOCK (src);
    src->photo->settings.ev_compensation = ev_comp;
    GST_OBJECT_UNLOCK (src);
  }

  return ret;
}

static gboolean
gst_droidcamsrc_set_iso_speed (GstDroidCamSrc * src, guint iso_speed)
{
  int x;
  int len = g_list_length (src->photo->iso);
  gchar *value = NULL;

  for (x = 0; x < len; x++) {
    struct DataEntry *entry =
        (struct DataEntry *) g_list_nth_data (src->photo->iso, x);
    if (iso_speed >= entry->key) {
      value = entry->value;
      break;
    }
  }

  if (!value) {
    GST_WARNING_OBJECT (src, "setting iso to %d is not supported", iso_speed);
    return FALSE;
  }

  GST_OBJECT_LOCK (src);
  src->photo->settings.iso_speed = iso_speed;
  GST_OBJECT_UNLOCK (src);

  return gst_droidcamsrc_set_and_apply (src, "iso", value);
}

static gboolean
gst_droidcamsrc_set_aperture (GstDroidCamSrc * src, guint aperture)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_set_exposure (GstDroidCamSrc * src, guint32 exposure)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_set_white_balance_mode (GstDroidCamSrc *
    src, GstPhotographyWhiteBalanceMode wb_mode)
{
  SET_ENUM (src->photo->wb, wb_mode, "whitebalance", wb_mode);
}

static gboolean
gst_droidcamsrc_set_color_tone_mode (GstDroidCamSrc *
    src, GstPhotographyColorToneMode tone_mode)
{
  SET_ENUM (src->photo->color_tone, tone_mode, "effect", tone_mode);
}

static gboolean
gst_droidcamsrc_set_scene_mode (GstDroidCamSrc
    * src, GstPhotographySceneMode scene_mode)
{
  // TODO: an idea would be switching focus mode to macro here if we are in closeup scene
  // and switch it back to whatever it was when we are in normal mode.
  SET_ENUM (src->photo->scene, scene_mode, "scene-mode", scene_mode);
}

static gboolean
gst_droidcamsrc_set_flash_mode (GstDroidCamSrc
    * src, GstPhotographyFlashMode flash_mode)
{
  SET_ENUM (src->photo->flash, flash_mode, "flash-mode", flash_mode);
}

static gboolean
gst_droidcamsrc_set_zoom (GstDroidCamSrc * src, gfloat zoom)
{
  int step = zoom;
  int max_zoom;
  gboolean ret;
  gchar *value;

  GST_OBJECT_LOCK (src);
  max_zoom = src->max_zoom;
  GST_OBJECT_UNLOCK (src);

  if (step > max_zoom) {
    GST_WARNING_OBJECT (src, "requested zoom (%d) is larger than max zoom (%d)",
        step, max_zoom);
    return FALSE;
  }

  GST_OBJECT_LOCK (src);
  src->photo->settings.zoom = zoom;
  GST_OBJECT_UNLOCK (src);

  step -= 1;
  value = g_strdup_printf ("%d", step);
  ret = gst_droidcamsrc_set_and_apply (src, "zoom", value);

  GST_DEBUG_OBJECT (src, "zoom set to %s", value);

  g_free (value);

  return ret;
}

static gboolean
gst_droidcamsrc_set_flicker_mode (GstDroidCamSrc * src,
    GstPhotographyFlickerReductionMode flicker_mode)
{
  SET_ENUM (src->photo->flicker, flicker_mode, "antibanding", flicker_mode);
}

static gboolean
gst_droidcamsrc_set_focus_mode (GstDroidCamSrc
    * src, GstPhotographyFocusMode focus_mode)
{
  int x;
  int len = g_list_length (src->photo->focus);
  const gchar *value = NULL;
  for (x = 0; x < len; x++) {
    struct DataEntry *entry =
        (struct DataEntry *) g_list_nth_data (src->photo->focus, x);
    if (focus_mode == entry->key) {
      value = entry->value;
      break;
    }
  }

  if (!value) {
    GST_WARNING_OBJECT (src, "setting focus-mode to %d is not supported",
        focus_mode);
    return FALSE;
  }

  GST_OBJECT_LOCK (src);
  src->photo->settings.focus_mode = focus_mode;
  GST_OBJECT_UNLOCK (src);

  if (g_strcmp0 (value, "continuous")) {
    return gst_droidcamsrc_set_and_apply (src, "focus-mode", value);
  }

  if (src->mode == MODE_IMAGE) {
    return gst_droidcamsrc_set_and_apply (src, "focus-mode",
        "continuous-picture");
  } else {
    return gst_droidcamsrc_set_and_apply (src, "focus-mode",
        "continuous-video");
  }
}

static GstPhotographyCaps
gst_droidcamsrc_get_capabilities (GstDroidCamSrc * src)
{
  return GST_PHOTOGRAPHY_CAPS_FLASH | GST_PHOTOGRAPHY_CAPS_FOCUS |
      GST_PHOTOGRAPHY_CAPS_ISO_SPEED | GST_PHOTOGRAPHY_CAPS_ZOOM |
      GST_PHOTOGRAPHY_CAPS_EV_COMP | GST_PHOTOGRAPHY_CAPS_WB_MODE |
      GST_PHOTOGRAPHY_CAPS_SCENE | GST_PHOTOGRAPHY_CAPS_TONE |
      GST_PHOTOGRAPHY_CAPS_FLICKER_REDUCTION;
}

static gboolean
gst_droidcamsrc_prepare_for_capture (GstDroidCamSrc *
    src, GstPhotographyCapturePrepared func, GstCaps * capture_caps,
    gpointer user_data)
{
  return TRUE;
}

static void
gst_droidcamsrc_set_autofocus (GstDroidCamSrc * src, gboolean on)
{
  GST_DEBUG_OBJECT (src, "set autofocus %d", on);

  if (!src->dev) {
    GST_WARNING_OBJECT (src, "camera is not running");
    return;
  }

  if (on) {
    if (!gst_droidcamsrc_dev_start_autofocus (src->dev)) {
      GST_WARNING_OBJECT (src, "failed to start autofocus");
    }
  } else {
    gst_droidcamsrc_dev_stop_autofocus (src->dev);
  }
}

static gboolean
gst_droidcamsrc_set_config (GstDroidCamSrc *
    src, GstPhotographySettings * config)
{
  // TODO:
  return FALSE;
}

static gboolean
gst_droidcamsrc_get_config (GstDroidCamSrc *
    src, GstPhotographySettings * config)
{
  GST_OBJECT_LOCK (src);
  memcpy (config, &src->photo->settings, sizeof (GstPhotographySettings));
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_droidcamsrc_set_noise_reduction (GstDroidCamSrc *
    src, GstPhotographyNoiseReduction noise_reduction)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_set_exposure_mode (GstDroidCamSrc *
    src, GstPhotographyExposureMode exposure_mode)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_set_analog_gain (GstDroidCamSrc * src, gfloat analog_gain)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_set_lens_focus (GstDroidCamSrc * src, gfloat lens_focus)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_set_color_temperature (GstDroidCamSrc * src,
    guint color_temperature)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_set_min_exposure_time (GstDroidCamSrc * src,
    guint min_exposure_time)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_set_max_exposure_time (GstDroidCamSrc * src,
    guint max_exposure_time)
{
  /* not supported */

  return FALSE;
}

static gboolean
gst_droidcamsrc_set_and_apply (GstDroidCamSrc * src, const gchar * key,
    const gchar * value)
{
  GST_INFO_OBJECT (src, "setting %s to %s", key, value);

  if (!src->dev || !src->dev->params) {
    return TRUE;
  }

  gst_droidcamsrc_params_set_string (src->dev->params, key, value);

  return gst_droidcamsrc_apply_params (src);
}

void
gst_droidcamsrc_photography_set_focus_to_droid (GstDroidCamSrc * src)
{
  int x;
  int len = g_list_length (src->photo->focus);
  gchar *value = NULL;

  if (!src->dev || !src->dev->params) {
    return;
  }

  for (x = 0; x < len; x++) {
    struct DataEntry *entry =
        (struct DataEntry *) g_list_nth_data (src->photo->focus, x);
    if (src->photo->settings.focus_mode == entry->key) {
      value = entry->value;
      break;
    }
  }

  if (!value) {
    GST_WARNING_OBJECT (src, "setting focus-mode to %d is not supported",
        src->photo->settings.focus_mode);
    return;
  }

  if (g_strcmp0 (value, "continuous")) {
    gst_droidcamsrc_params_set_string (src->dev->params, "focus-mode", value);
    return;
  }

  if (src->mode == MODE_IMAGE) {
    gst_droidcamsrc_params_set_string (src->dev->params, "focus-mode",
        "continuous-picture");
  } else {
    gst_droidcamsrc_params_set_string (src->dev->params, "focus-mode",
        "continuous-video");
  }
}

void
gst_droidcamsrc_photography_set_flash_to_droid (GstDroidCamSrc * src)
{
  int x;
  int len = g_list_length (src->photo->flash);
  gchar *value = NULL;

  if (!src->dev || !src->dev->params) {
    return;
  }

  if (src->mode == MODE_VIDEO) {
    if (src->video_torch) {
      gst_droidcamsrc_params_set_string (src->dev->params, "flash-mode",
          "torch");
    } else {
      gst_droidcamsrc_params_set_string (src->dev->params, "flash-mode", "off");
    }
    return;
  }

  for (x = 0; x < len; x++) {
    struct DataEntry *entry =
        (struct DataEntry *) g_list_nth_data (src->photo->flash, x);
    if (src->photo->settings.flash_mode == entry->key) {
      value = entry->value;
      break;
    }
  }

  if (!value) {
    GST_WARNING_OBJECT (src, "setting flash-mode to %d is not supported",
        src->photo->settings.flash_mode);
    return;
  }

  GST_INFO_OBJECT (src, "setting flash-mode to %s", value);
  gst_droidcamsrc_params_set_string (src->dev->params, "flash-mode", value);
}

static void
gst_droidcamsrc_photography_set_iso_to_droid (GstDroidCamSrc * src)
{
  int x;
  int len = g_list_length (src->photo->iso);
  gchar *value = NULL;

  for (x = 0; x < len; x++) {
    struct DataEntry *entry =
        (struct DataEntry *) g_list_nth_data (src->photo->iso, x);
    if (src->photo->settings.iso_speed >= entry->key) {
      value = entry->value;
      break;
    }
  }

  if (!value) {
    GST_WARNING_OBJECT (src, "setting iso to %d is not supported",
        src->photo->settings.iso_speed);
    return;
  }

  gst_droidcamsrc_params_set_string (src->dev->params, "iso", value);
}

static void
gst_droidcamsrc_photography_set_zoom_to_droid (GstDroidCamSrc * src)
{
  gchar *value;
  int step = src->photo->settings.zoom;

  step -= 1;

  value = g_strdup_printf ("%d", step);

  GST_DEBUG_OBJECT (src, "zoom set to %s", value);

  gst_droidcamsrc_params_set_string (src->dev->params, "zoom", value);

  g_free (value);
}

static void
gst_droidcamsrc_photography_set_ev_compensation_to_droid (GstDroidCamSrc * src)
{
  int val;
  gchar *value;
  gfloat ev_comp;

  if (src->ev_step == 0) {
    GST_DEBUG_OBJECT (src,
        "Cannot set exposure compensation because ev_step is still unknown.");
    return;
  }

  ev_comp =
      CLAMP (src->photo->settings.ev_compensation, src->min_ev_compensation,
      src->max_ev_compensation);
  val = ev_comp / src->ev_step;

  value = g_strdup_printf ("%d", val);

  GST_INFO_OBJECT (src, "setting exposure-compensation to %s", value);

  gst_droidcamsrc_params_set_string (src->dev->params, "exposure-compensation",
      value);

  g_free (value);
}
