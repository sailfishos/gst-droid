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

#include "gstdroidcodec.h"

GST_DEFINE_MINI_OBJECT_TYPE (GstDroidCodec, gst_droid_codec);

static GstDroidCodec *codec = NULL;
G_LOCK_DEFINE_STATIC (codec);

static void
gst_droid_codec_destroy_component (GstDroidComponent * component)
{
  /* close */

  /* unload */

  /* free */
  g_slice_free (GstDroidComponent, component);
}

GstDroidComponent *
gst_droid_codec_get_component (GstDroidCodec * codec, const gchar * type)
{
  GstDroidComponent *component;

  g_mutex_lock (&codec->lock);

  if (g_hash_table_contains (codec->cores, type)) {
    component = (GstDroidComponent *) g_hash_table_lookup (codec->cores, type);
    component->count++;
  } else {
    /* read info from configuration */

    /* allocate */
    component = g_slice_new (GstDroidComponent);

    /* dlopen */

    /* init */

  }

  g_mutex_unlock (&codec->lock);

  return component;
}

void
gst_droid_codec_put_component (GstDroidCodec * codec, const gchar * type)
{
  GstDroidComponent *component;

  g_mutex_lock (&codec->lock);

  component = (GstDroidComponent *) g_hash_table_lookup (codec->cores, type);

  if (component->count > 1) {
    component->count--;
  } else {
    g_hash_table_remove (codec->cores, type);
  }

  g_mutex_unlock (&codec->lock);
}

static void
gst_droid_codec_free ()
{
  G_LOCK (codec);

  g_mutex_clear (&codec->lock);
  g_hash_table_unref (codec->cores);
  g_slice_free (GstDroidCodec, codec);
  codec = NULL;

  G_UNLOCK (codec);
}

GstDroidCodec *
gst_droid_codec_get (void)
{
  G_LOCK (codec);

  if (!codec) {
    codec = g_slice_new (GstDroidCodec);
    codec->cores = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
        (GDestroyNotify) gst_droid_codec_destroy_component);
    g_mutex_init (&codec->lock);
    gst_mini_object_init (GST_MINI_OBJECT_CAST (codec), 0, GST_TYPE_DROID_CODEC,
        NULL, NULL, (GstMiniObjectFreeFunction) gst_droid_codec_free);
  }

  G_UNLOCK (codec);

  return codec;
}
