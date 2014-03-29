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
#include "binding.h"
#include <dlfcn.h>

GST_DEFINE_MINI_OBJECT_TYPE (GstDroidCodec, gst_droid_codec);

static GstDroidCodec *codec = NULL;
G_LOCK_DEFINE_STATIC (codec);

// TODO: hardcoded
#define CONFIG_DIR   "/etc/gst-droid/droidcodec.d"

struct _GstDroidCodecHandle
{
  void *handle;

  int count;

    OMX_ERRORTYPE (*init) (void);
    OMX_ERRORTYPE (*deinit) (void);
};

static void
gst_droid_codec_destroy_component (GstDroidComponent * component)
{
  OMX_ERRORTYPE err;

  /* deinit */
  if (component->handle->deinit) {
    err = component->handle->deinit ();
    if (err != OMX_ErrorNone) {
      // TODO:
    }
  }

  /* unload */
  if (component->handle) {
    android_dlclose (component->handle);
    component->handle = NULL;
  }

  /* free */
  g_slice_free (GstDroidCodecHandle, component->handle);
  g_slice_free (GstDroidComponent, component);
}

GstDroidComponent *
gst_droid_codec_get_component (GstDroidCodec * codec, const gchar * type)
{
  OMX_ERRORTYPE err;
  gboolean res;
  GstDroidCodecHandle *handle = NULL;
  GstDroidComponent *component = NULL;
  gchar *path = NULL;
  GKeyFile *file = NULL;
  gchar *core_path = NULL;
  int in_port = 0;
  int out_port = 0;
  gchar *name = NULL;

  g_mutex_lock (&codec->lock);

  if (g_hash_table_contains (codec->cores, type)) {
    component = (GstDroidComponent *) g_hash_table_lookup (codec->cores, type);
    component->handle->count++;
    goto unlock_and_out;
  }

  file = g_key_file_new ();
  path = g_strdup_printf ("%s/%s.conf", CONFIG_DIR, type);

  /* read info from configuration */
  res = g_key_file_load_from_file (file, path, 0, NULL);
  if (!res) {
    goto unlock_and_out;
  }

  core_path = g_key_file_get_string (file, "droidcodec", "core", NULL);
  if (!core_path) {
    goto unlock_and_out;
  }

  name = g_key_file_get_string (file, "droidcodec", "component", NULL);
  if (!name) {
    goto unlock_and_out;
  }

  in_port = g_key_file_get_integer (file, "droidcodec", "in-port", NULL);
  out_port = g_key_file_get_integer (file, "droidcodec", "out-port", NULL);

  if (in_port == out_port) {
    goto unlock_and_out;
  }

  /* allocate */
  handle = g_slice_new0 (GstDroidCodecHandle);
  component = g_slice_new0 (GstDroidComponent);
  component->handle = handle;

  /* dlopen */
  handle->handle = android_dlopen (core_path, RTLD_NOW);
  if (!handle->handle) {
    goto cleanup;
  }

  /* dlsym */
  component->handle->init = android_dlsym (handle->handle, "OMX_Init");
  component->handle->deinit = android_dlsym (handle->handle, "OMX_Deinit");
  component->get_handle = android_dlsym (handle->handle, "OMX_GetHandle");
  component->free_handle = android_dlsym (handle->handle, "OMX_FreeHandle");

  if (!component->handle->init) {
    goto cleanup;
  }

  if (!component->handle->deinit) {
    goto cleanup;
  }

  if (!component->get_handle) {
    goto cleanup;
  }

  if (!component->free_handle) {
    goto cleanup;
  }

  /* init */
  err = component->handle->init ();
  if (err != OMX_ErrorNone) {
    goto cleanup;
  }

  /* insert */
  g_hash_table_insert (codec->cores, (gpointer) g_strdup (type), component);
  goto unlock_and_out;

cleanup:
  gst_droid_codec_destroy_component (component);
  component = NULL;

unlock_and_out:
  g_mutex_unlock (&codec->lock);

  if (file) {
    g_key_file_unref (file);
  }

  if (path) {
    g_free (path);
  }

  if (name) {
    g_free (name);
  }

  if (core_path) {
    g_free (core_path);
  }

  return component;
}

void
gst_droid_codec_put_component (GstDroidCodec * codec, const gchar * type)
{
  GstDroidComponent *component;

  g_mutex_lock (&codec->lock);

  component = (GstDroidComponent *) g_hash_table_lookup (codec->cores, type);

  if (component->handle->count > 1) {
    component->handle->count--;
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
