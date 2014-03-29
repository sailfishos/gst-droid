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

  gchar *type;
  gchar *role;
  gchar *name;

    OMX_ERRORTYPE (*init) (void);
    OMX_ERRORTYPE (*deinit) (void);
    OMX_ERRORTYPE (*get_handle) (OMX_HANDLETYPE * handle,
      OMX_STRING name, OMX_PTR data, OMX_CALLBACKTYPE * callbacks);
    OMX_ERRORTYPE (*free_handle) (OMX_HANDLETYPE handle);
};

static void
gst_droid_codec_destroy_handle (GstDroidCodecHandle * handle)
{
  OMX_ERRORTYPE err;

  if (handle->type) {
    g_free (handle->type);
  }

  if (handle->role) {
    g_free (handle->role);
  }

  if (handle->name) {
    g_free (handle->name);
  }

  /* deinit */
  if (handle->deinit) {
    err = handle->deinit ();
    if (err != OMX_ErrorNone) {
      // TODO:
    }
  }

  /* unload */
  if (handle->handle) {
    /* TODO: this is crashing */
    /*    android_dlclose (handle->handle); */
    handle->handle = NULL;
  }

  /* free */
  g_slice_free (GstDroidCodecHandle, handle);
}

static GstDroidCodecHandle *
gst_droid_codec_create_and_insert_handle_locked (GstDroidCodec * codec,
    const gchar * type)
{
  OMX_ERRORTYPE err;
  gboolean res;
  gchar *path = NULL;
  GKeyFile *file = NULL;
  gchar *core_path = NULL;
  int in_port = 0;
  int out_port = 0;
  gchar *name = NULL;
  gchar *role = NULL;
  GstDroidCodecHandle *handle = NULL;

  file = g_key_file_new ();
  path = g_strdup_printf ("%s/%s.conf", CONFIG_DIR, type);

  /* read info from configuration */
  res = g_key_file_load_from_file (file, path, 0, NULL);
  if (!res) {
    goto error;
  }

  core_path = g_key_file_get_string (file, "droidcodec", "core", NULL);
  if (!core_path) {
    goto error;
  }

  name = g_key_file_get_string (file, "droidcodec", "component", NULL);
  if (!name) {
    goto error;
  }

  role = g_key_file_get_string (file, "droidcodec", "role", NULL);
  if (!role) {
    goto error;
  }

  in_port = g_key_file_get_integer (file, "droidcodec", "in-port", NULL);
  out_port = g_key_file_get_integer (file, "droidcodec", "out-port", NULL);

  if (in_port == out_port) {
    goto error;
  }

  handle = g_slice_new0 (GstDroidCodecHandle);
  handle->count = 1;
  handle->type = g_strdup (type);
  handle->role = g_strdup (role);
  handle->name = g_strdup (name);

  handle->handle = android_dlopen (core_path, RTLD_NOW);
  if (!handle->handle) {
    goto error;
  }

  /* dlsym */
  handle->init = android_dlsym (handle->handle, "OMX_Init");
  handle->deinit = android_dlsym (handle->handle, "OMX_Deinit");
  handle->get_handle = android_dlsym (handle->handle, "OMX_GetHandle");
  handle->free_handle = android_dlsym (handle->handle, "OMX_FreeHandle");

  if (!handle->init) {
    goto error;
  }

  if (!handle->deinit) {
    goto error;
  }

  if (!handle->get_handle) {
    goto error;
  }

  if (!handle->free_handle) {
    goto error;
  }

  err = handle->init ();
  if (err != OMX_ErrorNone) {
    goto error;
  }

  g_hash_table_insert (codec->cores, (gpointer) g_strdup (type), handle);

  return handle;

error:
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

  if (role) {
    g_free (role);
  }

  /* unset deinit to prevent calling it from _destroy_handle */
  handle->deinit = NULL;
  gst_droid_codec_destroy_handle (handle);
  return NULL;
}

static void
gst_droid_codec_destroy_component (GstDroidComponent * component)
{
  /* free */
  g_slice_free (GstDroidComponent, component);
}

GstDroidComponent *
gst_droid_codec_get_component (GstDroidCodec * codec, const gchar * type)
{
  GstDroidComponent *component = NULL;
  GstDroidCodecHandle *handle;

  g_mutex_lock (&codec->lock);

  if (g_hash_table_contains (codec->cores, type)) {
    handle = (GstDroidCodecHandle *) g_hash_table_lookup (codec->cores, type);
    handle->count++;
  } else {
    handle = gst_droid_codec_create_and_insert_handle_locked (codec, type);
    if (!handle) {
      goto error;
    }
  }

  /* allocate */
  component = g_slice_new0 (GstDroidComponent);
  component->handle = handle;

  goto unlock_and_out;

error:
  gst_droid_codec_destroy_component (component);
  component = NULL;

unlock_and_out:
  g_mutex_unlock (&codec->lock);

  return component;
}

void
gst_droid_codec_put_component (GstDroidCodec * codec,
    GstDroidComponent * component)
{
  g_mutex_lock (&codec->lock);

  if (component->handle->count > 1) {
    component->handle->count--;
  } else {
    g_hash_table_remove (codec->cores, component->handle->type);
  }

  g_mutex_unlock (&codec->lock);

  gst_droid_codec_destroy_component (component);
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
        (GDestroyNotify) gst_droid_codec_destroy_handle);
    g_mutex_init (&codec->lock);
    gst_mini_object_init (GST_MINI_OBJECT_CAST (codec), 0, GST_TYPE_DROID_CODEC,
        NULL, NULL, (GstMiniObjectFreeFunction) gst_droid_codec_free);
  }

  G_UNLOCK (codec);

  return codec;
}
