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

#include "gstdroidcamsrcphotography.h"
#include "gstdroidcamsrc.h"
#include <stdlib.h>
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif /* GST_USE_UNSTABLE_API */
#include <gst/interfaces/photography.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droidcamsrc_debug);
#define GST_CAT_DEFAULT gst_droidcamsrc_debug

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
};

void
gst_droidcamsrc_photography_register (gpointer g_iface, gpointer iface_data)
{
  // TODO:

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
    GValue * value, GParamSpec * pspec)
{
  // TODO:
  switch (prop_id) {
    case PROP_WB_MODE:
    case PROP_COLOR_TONE:
    case PROP_SCENE_MODE:
    case PROP_FLASH_MODE:
    case PROP_FLICKER_MODE:
    case PROP_FOCUS_MODE:
    case PROP_NOISE_REDUCTION:
    case PROP_EXPOSURE_MODE:
    case PROP_ZOOM:
    case PROP_EV_COMP:
    case PROP_ANALOG_GAIN:
    case PROP_LENS_FOCUS:
    case PROP_APERTURE:
    case PROP_ISO_SPEED:
    case PROP_COLOR_TEMPERATURE:
    case PROP_MIN_EXPOSURE_TIME:
    case PROP_MAX_EXPOSURE_TIME:
    case PROP_EXPOSURE_TIME:
    case PROP_CAPABILITIES:
    case PROP_IMAGE_CAPTURE_SUPPORTED_CAPS:
    case PROP_IMAGE_PREVIEW_SUPPORTED_CAPS:
    case PROP_WHITE_POINT:
      return TRUE;
  }

  return FALSE;
}

gboolean
gst_droidcamsrc_photography_set_property (GstDroidCamSrc * src, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  // TODO:

  switch (prop_id) {
    case PROP_WB_MODE:
    case PROP_COLOR_TONE:
    case PROP_SCENE_MODE:
    case PROP_FLASH_MODE:
    case PROP_FLICKER_MODE:
    case PROP_FOCUS_MODE:
    case PROP_EV_COMP:
    case PROP_ISO_SPEED:
    case PROP_APERTURE:
    case PROP_EXPOSURE_TIME:
    case PROP_ZOOM:
    case PROP_COLOR_TEMPERATURE:
    case PROP_WHITE_POINT:
    case PROP_ANALOG_GAIN:
    case PROP_LENS_FOCUS:
    case PROP_MIN_EXPOSURE_TIME:
    case PROP_MAX_EXPOSURE_TIME:
    case PROP_NOISE_REDUCTION:
    case PROP_EXPOSURE_MODE:
      return TRUE;
  }

  return FALSE;
}

void
gst_droidcamsrc_photography_init (GstDroidCamSrc * src)
{
  GKeyFile *file = g_key_file_new ();
  gchar *file_path =
      g_build_path ("/", SYSCONFDIR, "gst-droid/gstdroidcamsrc.conf", NULL);
  GError *err = NULL;

  src->photo = g_slice_new0 (GstDroidCamSrcPhotography);

  if (!g_key_file_load_from_file (file, file_path, G_KEY_FILE_NONE, &err)) {
    GST_WARNING ("failed to load configuration file %s: %s", file_path,
        err->message);
  }

  if (err) {
    g_error_free (err);
    err = NULL;
  }

  src->photo->settings.wb_mode = GST_PHOTOGRAPHY_WB_MODE_AUTO;
  src->photo->settings.tone_mode = GST_PHOTOGRAPHY_COLOR_TONE_MODE_NORMAL;
  src->photo->settings.scene_mode = GST_PHOTOGRAPHY_SCENE_MODE_AUTO;
  src->photo->settings.flash_mode = GST_PHOTOGRAPHY_FLASH_MODE_AUTO;
  src->photo->settings.exposure_time = 0;
  src->photo->settings.aperture = 0;
  src->photo->settings.ev_compensation = 0.0;
  src->photo->settings.iso_speed = 0;
  src->photo->settings.zoom = 1.0;
  src->photo->settings.flicker_mode = GST_PHOTOGRAPHY_FLICKER_REDUCTION_OFF;
  src->photo->settings.focus_mode =
      GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL;

  /* TODO: the ones below are the ones I am not sure how to set a default value for */
  src->photo->settings.noise_reduction = 0;
  src->photo->settings.exposure_mode = GST_PHOTOGRAPHY_EXPOSURE_MODE_AUTO;      /* TODO: seems not to be a property */
  src->photo->settings.color_temperature = 0;
  memset (&src->photo->settings.white_point, 0x0,
      sizeof (src->photo->settings.white_point));
  src->photo->settings.analog_gain = 0.0;
  src->photo->settings.lens_focus = 0.0;
  src->photo->settings.min_exposure_time = 0;
  src->photo->settings.max_exposure_time = 0;

  /* free our stuff */
  g_free (file_path);
  g_key_file_unref (file);
}

void
gst_droidcamsrc_photography_destroy (GstDroidCamSrc * src)
{
  g_slice_free (GstDroidCamSrcPhotography, src->photo);
  src->photo = NULL;
}
