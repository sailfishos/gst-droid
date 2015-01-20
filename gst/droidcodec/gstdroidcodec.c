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
#include <string.h>
#include <unistd.h>
#include "HardwareAPI.h"
#include "gstdroidcodecallocatoromx.h"
#include "gstdroidcodecallocatorgralloc.h"
#include "gst/memory/gstgralloc.h"
#include "gstdroidcodectype.h"
#include "plugin.h"
#include "gstencoderparams.h"
#include "droidmediacodec.h"

GST_DEFINE_MINI_OBJECT_TYPE (GstDroidCodec, gst_droid_codec);

GST_DEBUG_CATEGORY_EXTERN (gst_droid_codec_debug);
#define GST_CAT_DEFAULT gst_droid_codec_debug

static GstDroidCodec *codec = NULL;
G_LOCK_DEFINE_STATIC (codec);

/* 10 ms */
#define WAIT_TIMEOUT 10000

struct _GstDroidCodecHandle
{
  void *handle;

  int count;

  gchar *type;
  gchar *role;
  gchar *name;

  gboolean is_decoder;

  int in_port;
  int out_port;

    OMX_ERRORTYPE (*init) (void);
    OMX_ERRORTYPE (*deinit) (void);
    OMX_ERRORTYPE (*get_handle) (OMX_HANDLETYPE * handle,
      OMX_STRING name, OMX_PTR data, OMX_CALLBACKTYPE * callbacks);
    OMX_ERRORTYPE (*free_handle) (OMX_HANDLETYPE handle);
};

typedef struct _CodecProfileLevel
{
  OMX_U32 mProfile;
  OMX_U32 mLevel;
} CodecProfileLevel;

typedef struct
{
  GstBuffer *buffer;
  GstMapInfo info;
  GstVideoCodecFrame *frame;
} DroidBufferCallbackMapInfo;

static OMX_ERRORTYPE
EventHandler (OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent,
    OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData)
{
  GstDroidComponent *comp = (GstDroidComponent *) pAppData;

  GST_DEBUG_OBJECT (comp->parent,
      "got OMX event of type 0x%x (arg1 = 0x%lx, arg2 = 0x%lx)", eEvent, nData1,
      nData2);

  // TODO:
  switch (eEvent) {
    case OMX_EventCmdComplete:
      if (nData1 == OMX_CommandStateSet) {
        GST_INFO_OBJECT (comp->parent, "component reached state %s",
            gst_omx_state_to_string (nData2));
        g_atomic_int_set (&comp->state, nData2);
      }

      break;
    case OMX_EventError:
      if (nData1 != OMX_ErrorNone) {
        GST_ERROR_OBJECT (comp->parent, "error %s from omx",
            gst_omx_error_to_string (nData1));
        g_mutex_lock (&comp->lock);
        comp->error = TRUE;
        g_mutex_unlock (&comp->lock);
      }

      break;
    case OMX_EventPortSettingsChanged:
      if (nData1 != 1) {
        GST_ERROR_OBJECT (comp->parent,
            "OMX_EventPortSettingsChanged not supported on input port");
        g_mutex_lock (&comp->lock);
        comp->error = TRUE;
        g_mutex_unlock (&comp->lock);
      } else {
        GST_INFO_OBJECT (comp->parent, "component needs to be reconfigured");
        g_mutex_lock (&comp->lock);
        comp->needs_reconfigure = TRUE;
        g_mutex_unlock (&comp->lock);
      }

      break;
    case OMX_EventPortFormatDetected:
      g_print ("OMX_EventPortFormatDetected\n");
      break;
    case OMX_EventBufferFlag:
      g_print ("OMX_EventBufferFlag\n");
      break;
    default:
      break;
  }

  //  g_print ("EventHandler %d\n", eEvent);

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE
EmptyBufferDone (OMX_HANDLETYPE hComponent, OMX_PTR pAppPrivate,
    OMX_BUFFERHEADERTYPE * pBuffer)
{
  GstDroidComponent *comp;
  GstBuffer *buffer = NULL;

  comp = (GstDroidComponent *) pAppPrivate;

  GST_DEBUG_OBJECT (comp->parent, "EmptyBufferDone");

  if (pBuffer->pAppPrivate) {
    buffer = (GstBuffer *) pBuffer->pAppPrivate;
  }

  /* reset buffer */
  pBuffer->nFilledLen = 0;
  pBuffer->nOffset = 0;
  pBuffer->nFlags = 0;

  if (buffer) {
    GST_DEBUG ("buffer %p emptied and being returned", buffer);
    gst_buffer_unref (buffer);
  }

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE
FillBufferDone (OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE * pBuffer)
{
  GstDroidComponent *comp;

  comp = (GstDroidComponent *) pAppData;

  GST_DEBUG_OBJECT (comp->parent, "fillBufferDone %p", pBuffer);

  g_mutex_lock (&comp->full_lock);
  g_queue_push_tail (comp->full, pBuffer);
  g_cond_signal (&comp->full_cond);
  g_mutex_unlock (&comp->full_lock);

  return OMX_ErrorNone;
}

static OMX_CALLBACKTYPE callbacks =
    { EventHandler, EmptyBufferDone, FillBufferDone };

static void
gst_droid_codec_destroy_handle (GstDroidCodecHandle * handle)
{
  OMX_ERRORTYPE err;

  GST_DEBUG ("destroying handle %p", handle);

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
  gboolean is_decoder;
  GError *error = NULL;

  GST_DEBUG ("create and insert handle locked");

  file = g_key_file_new ();
  path = gst_droid_codec_type_get_path (type);

  /* read info from configuration */
  res = g_key_file_load_from_file (file, path, 0, &error);
  if (!res) {
    GST_ERROR ("error %s reading %s", error->message, path);
    goto error;
  }

  core_path = g_key_file_get_string (file, "droidcodec", "core", &error);
  if (!core_path) {
    GST_ERROR ("error %s reading %s", error->message, path);
    goto error;
  }

  name = g_key_file_get_string (file, "droidcodec", "component", &error);
  if (!name) {
    GST_ERROR ("error %s reading %s", error->message, path);
    goto error;
  }

  is_decoder = gst_droid_codec_type_get_type (type) == GST_DROID_CODEC_DECODER;

  role = g_key_file_get_string (file, "droidcodec", "role", &error);
  if (!role) {
    GST_ERROR ("error %s reading %s", error->message, path);
    goto error;
  }

  in_port = g_key_file_get_integer (file, "droidcodec", "in-port", &error);
  if (error) {
    GST_ERROR ("error %s reading %s", error->message, path);
    goto error;
  }

  out_port = g_key_file_get_integer (file, "droidcodec", "out-port", &error);
  if (error) {
    GST_ERROR ("error %s reading %s", error->message, path);
  }

  if (in_port == out_port) {
    GST_ERROR ("in port and out port can not be equal");
    goto error;
  }

  handle = g_slice_new0 (GstDroidCodecHandle);
  handle->count = 1;
  handle->type = g_strdup (type);
  handle->role = g_strdup (role);
  handle->name = g_strdup (name);
  handle->in_port = in_port;
  handle->out_port = out_port;
  handle->is_decoder = is_decoder;
  handle->handle = android_dlopen (core_path, RTLD_NOW);
  if (!handle->handle) {
    GST_ERROR ("error loading core %s", core_path);
    goto error;
  }

  /* dlsym */
  handle->init = android_dlsym (handle->handle, "OMX_Init");
  handle->deinit = android_dlsym (handle->handle, "OMX_Deinit");
  handle->get_handle = android_dlsym (handle->handle, "OMX_GetHandle");
  handle->free_handle = android_dlsym (handle->handle, "OMX_FreeHandle");

  if (!handle->init) {
    GST_ERROR ("OMX_Init not found");
    goto error;
  }

  if (!handle->deinit) {
    GST_ERROR ("OMX_Deinit not found");
    goto error;
  }

  if (!handle->get_handle) {
    GST_ERROR ("OMX_GetHandle not found");
    goto error;
  }

  if (!handle->free_handle) {
    GST_ERROR ("OMX_FreeHandle not found");
    goto error;
  }

  err = handle->init ();
  if (err != OMX_ErrorNone) {
    GST_ERROR ("got error %s (0x%08x) while initialization",
        gst_omx_error_to_string (err), err);
    goto error;
  }

  g_hash_table_insert (codec->cores, (gpointer) g_strdup (type), handle);

  GST_DEBUG ("created handle %p", handle);

  g_free (path);
  g_free (core_path);
  g_free (name);
  g_free (role);
  g_key_file_unref (file);

  return handle;

error:
  if (error) {
    g_error_free (error);
  }

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

void
gst_droid_codec_destroy_component (GstDroidComponent * component)
{
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (component->parent, "destroy component %p", component);

  if (component->omx) {
    err = component->handle->free_handle (component->omx);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (component->parent,
          "got error %s (0x%08x) freeing component handle",
          gst_omx_error_to_string (err), err);
    }
  }

  /* Let's take care of the handle */
  g_mutex_lock (&component->codec->lock);

  if (component->handle->count > 1) {
    component->handle->count--;
  } else {
    g_hash_table_remove (codec->cores, component->handle->type);
  }

  g_mutex_unlock (&component->codec->lock);

  /* free */
  gst_mini_object_unref (GST_MINI_OBJECT (component->codec));
  g_mutex_clear (&component->lock);
  g_queue_free (component->full);
  g_mutex_clear (&component->full_lock);
  g_cond_clear (&component->full_cond);
  g_slice_free (GstDroidComponentPort, component->in_port);
  g_slice_free (GstDroidComponentPort, component->out_port);
  g_slice_free (GstDroidComponent, component);
}

static gboolean
gst_droid_codec_enable_android_native_buffers (GstDroidComponent * comp,
    GstDroidComponentPort * port)
{
  OMX_ERRORTYPE err;
  OMX_INDEXTYPE extension;
  struct EnableAndroidNativeBuffersParams param;
  struct GetAndroidNativeBufferUsageParams usage_param;
  OMX_STRING ext1 = "OMX.google.android.index.enableAndroidNativeBuffers2";
  OMX_STRING ext2 = "OMX.google.android.index.getAndroidNativeBufferUsage";

  GST_DEBUG_OBJECT (comp->parent, "enable android native buffers");

  /* enable */
  err = OMX_GetExtensionIndex (comp->omx, ext1, &extension);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while getting extension %s index",
        gst_omx_error_to_string (err), err, ext1);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = port->def.nPortIndex;
  param.enable = OMX_TRUE;
  err = gst_droid_codec_set_param (comp, extension, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while enabling android buffers",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  /* get usage for gralloc allocation */
  err = OMX_GetExtensionIndex (comp->omx, ext2, &extension);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while getting extension %s index",
        gst_omx_error_to_string (err), err, ext1);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&usage_param);
  usage_param.nPortIndex = port->def.nPortIndex;
  err = gst_droid_codec_get_param (comp, extension, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while getting native buffer usage",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  port->usage = usage_param.nUsage;

  return TRUE;
}

static gboolean
gst_droid_codec_enable_metadata_in_buffers (GstDroidComponent * comp,
    GstDroidComponentPort * port)
{
  OMX_ERRORTYPE err;
  OMX_INDEXTYPE extension;
  struct StoreMetaDataInBuffersParams param;
  OMX_STRING ext = "OMX.google.android.index.storeMetaDataInBuffers";

  GST_DEBUG_OBJECT (comp->parent, "enable metadata in buffers");

  err = OMX_GetExtensionIndex (comp->omx, ext, &extension);

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while getting extension %s index",
        gst_omx_error_to_string (err), err, ext);

    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = port->def.nPortIndex;
  param.bStoreMetaData = OMX_TRUE;

  err = gst_droid_codec_set_param (comp, extension, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while enabling metadata",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

GstDroidComponent *
gst_droid_codec_get_component (GstDroidCodec * codec, const gchar * type,
    GstElement * parent)
{
  GstDroidComponent *component = NULL;
  GstDroidCodecHandle *handle;
  OMX_ERRORTYPE err;
  if (!codec) {
    return NULL;
  }

  g_mutex_lock (&codec->lock);

  if (g_hash_table_contains (codec->cores, type)) {
    handle = (GstDroidCodecHandle *) g_hash_table_lookup (codec->cores, type);
    handle->count++;
  } else {
    handle = gst_droid_codec_create_and_insert_handle_locked (codec, type);
    if (!handle) {
      GST_ERROR_OBJECT (parent, "error getting codec %s", type);
      goto error;
    }
  }

  /* allocate */
  component = g_slice_new0 (GstDroidComponent);
  component->in_port = g_slice_new0 (GstDroidComponentPort);
  component->out_port = g_slice_new0 (GstDroidComponentPort);
  component->codec =
      (GstDroidCodec *) gst_mini_object_ref (GST_MINI_OBJECT (codec));
  component->handle = handle;
  component->parent = parent;
  component->full = g_queue_new ();
  g_mutex_init (&component->full_lock);
  g_cond_init (&component->full_cond);
  component->error = FALSE;
  component->needs_reconfigure = FALSE;
  component->started = FALSE;
  g_mutex_init (&component->lock);
  g_atomic_int_set (&component->state, OMX_StateLoaded);

  err =
      handle->get_handle (&component->omx, handle->name, component, &callbacks);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (component->parent,
        "got error %s (0x%08x) getting component handle",
        gst_omx_error_to_string (err), err);
    goto error;
  }

  /* Now create our ports. */
  component->in_port->usage = -1;
  GST_OMX_INIT_STRUCT (&component->in_port->def);
  component->in_port->def.nPortIndex = component->handle->in_port;
  component->in_port->comp = component;

  component->out_port->usage = -1;
  GST_OMX_INIT_STRUCT (&component->out_port->def);
  component->out_port->def.nPortIndex = component->handle->out_port;
  component->out_port->comp = component;

  if (component->handle->is_decoder) {
    /* enable usage of android native buffers on output port */
    if (!gst_droid_codec_enable_android_native_buffers (component,
            component->out_port)) {
      goto error;
    }
  } else {
    /* encoders get meta data usage enabled */
    if (!gst_droid_codec_enable_metadata_in_buffers (component,
            component->in_port)) {
      goto error;
    }
  }

  err =
      gst_droid_codec_get_param (component, OMX_IndexParamPortDefinition,
      &component->in_port->def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (component->parent,
        "got error %s (0x%08x) getting input port definition",
        gst_omx_error_to_string (err), err);
    goto error;
  }

  err =
      gst_droid_codec_get_param (component, OMX_IndexParamPortDefinition,
      &component->out_port->def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (component->parent,
        "got error %s (0x%08x) getting output port definition",
        gst_omx_error_to_string (err), err);
    goto error;
  }

  goto unlock_and_out;

error:
  gst_droid_codec_destroy_component (component);
  component = NULL;

unlock_and_out:
  g_mutex_unlock (&codec->lock);

  return component;
}

static void
gst_droid_codec_free ()
{
  GST_DEBUG ("codec free");

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
  GST_DEBUG ("codec get");

  G_LOCK (codec);

  if (!codec) {
    codec = g_slice_new0 (GstDroidCodec);
    codec->cores = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
        (GDestroyNotify) gst_droid_codec_destroy_handle);
    g_mutex_init (&codec->lock);
    gst_mini_object_init (GST_MINI_OBJECT_CAST (codec), 0, GST_TYPE_DROID_CODEC,
        NULL, NULL, (GstMiniObjectFreeFunction) gst_droid_codec_free);
  } else {
    gst_mini_object_ref (GST_MINI_OBJECT (codec));
  }

  G_UNLOCK (codec);

  return codec;
}

OMX_ERRORTYPE
gst_droid_codec_get_param (GstDroidComponent * comp, OMX_INDEXTYPE index,
    gpointer param)
{
  GST_DEBUG_OBJECT (comp->parent, "getting parameter at index 0x%08x", index);

  return OMX_GetParameter (comp->omx, index, param);
}

OMX_ERRORTYPE
gst_droid_codec_set_param (GstDroidComponent * comp, OMX_INDEXTYPE index,
    gpointer param)
{
  GST_DEBUG_OBJECT (comp->parent, "setting parameter at index 0x%08x", index);

  return OMX_SetParameter (comp->omx, index, param);
}

OMX_ERRORTYPE
gst_droid_codec_set_config (GstDroidComponent * comp,
    OMX_INDEXTYPE index, gpointer config)
{
  GST_DEBUG_OBJECT (comp->parent, "setting config at index 0x%08x", index);

  return OMX_SetConfig (comp->omx, index, config);
}

gboolean
gst_droid_codec_configure_component (GstDroidComponent * comp,
    const GstVideoInfo * info)
{
  OMX_ERRORTYPE err;
  OMX_PARAM_PORTDEFINITIONTYPE def = comp->in_port->def;

  GST_DEBUG_OBJECT (comp->parent, "configure component");

  def.format.video.nFrameWidth = info->width;
  def.format.video.nFrameHeight = info->height;

  if (info->fps_n == 0) {
    def.format.video.xFramerate = 0;
  } else {
    def.format.video.xFramerate = (info->fps_n << 16) / (info->fps_d);
  }

  err = gst_droid_codec_set_param (comp, OMX_IndexParamPortDefinition, &def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) setting input port definition",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  err =
      gst_droid_codec_get_param (comp, OMX_IndexParamPortDefinition,
      &comp->in_port->def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) getting input port definition",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  err =
      gst_droid_codec_get_param (comp, OMX_IndexParamPortDefinition,
      &comp->out_port->def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) getting output port definition",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_droid_codec_allocate_port_buffers (GstDroidComponent * comp,
    GstDroidComponentPort * port, GstCaps * caps)
{
  GstStructure *config;

  GST_DEBUG_OBJECT (comp->parent, "allocate port %li buffers",
      port->def.nPortIndex);

  if (port->usage == -1) {
    port->allocator = gst_droid_codec_allocator_omx_new (port);
    port->buffers = gst_buffer_pool_new ();
  } else {
    port->allocator = gst_droid_codec_allocator_gralloc_new (port);
    port->buffers = gst_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (port->buffers);
  gst_buffer_pool_config_set_params (config, caps, port->def.nBufferSize,
      port->def.nBufferCountActual, port->def.nBufferCountActual);
  gst_buffer_pool_config_set_allocator (config, port->allocator, NULL);

  if (!gst_buffer_pool_set_config (port->buffers, config)) {
    GST_ERROR_OBJECT (port->comp->parent,
        "failed to set buffer pool configuration");
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (port->buffers, TRUE)) {
    GST_ERROR_OBJECT (port->comp->parent, "failed to activate buffer pool");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_droid_codec_set_port_enabled (GstDroidComponent * comp, int index,
    gboolean enable)
{
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (comp->parent, "set port enabled: %d %d", index, enable);

  if (enable) {
    err = OMX_SendCommand (comp->omx, OMX_CommandPortEnable, index, NULL);
  } else {
    err = OMX_SendCommand (comp->omx, OMX_CommandPortDisable, index, NULL);
  }

  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while enabling or disabling port",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_droid_codec_wait_for_state (GstDroidComponent * comp,
    OMX_STATETYPE new_state)
{
  int x = 0;
  for (x = 0; x < 20; x++) {
    if (g_atomic_int_get (&comp->state) != new_state
        && !gst_droid_codec_has_error (comp)) {
      usleep (WAIT_TIMEOUT);
    }
  }

  if (gst_droid_codec_has_error (comp)) {
    return FALSE;
  }

  if (g_atomic_int_get (&comp->state) != new_state) {
    GST_WARNING_OBJECT (comp->parent, "timeout waiting for state change to %s",
        gst_omx_state_to_string (new_state));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_droid_codec_set_state (GstDroidComponent * comp, OMX_STATETYPE new_state)
{
  OMX_STATETYPE old_state;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (comp->parent, "setting state to %s",
      gst_omx_state_to_string (new_state));

  err = OMX_GetState (comp->omx, &old_state);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent, "error %s getting component state",
        gst_omx_error_to_string (err));
    return FALSE;
  }

  if (old_state == new_state) {
    GST_DEBUG_OBJECT (comp->parent, "new state is already the existing state");
    return TRUE;
  }

  if (new_state <= OMX_StateInvalid || new_state > OMX_StatePause) {
    GST_ERROR_OBJECT (comp->parent, "cannot handle transition from %s to %s",
        gst_omx_state_to_string (old_state),
        gst_omx_state_to_string (new_state));
    return FALSE;
  }

  err = OMX_SendCommand (comp->omx, OMX_CommandStateSet, new_state, NULL);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) while setting state to %s",
        gst_omx_error_to_string (err), err,
        gst_omx_state_to_string (new_state));
    return FALSE;
  }

  /* we can not do much here really */

  return TRUE;
}

gboolean
gst_droid_codec_start_component (GstDroidComponent * comp, GstCaps * sink,
    GstCaps * src)
{
  GST_DEBUG_OBJECT (comp->parent, "start");

  if (!gst_droid_codec_set_state (comp, OMX_StateIdle)) {
    return FALSE;
  }

  /* TODO: not sure when to enable the ports */
  if (!gst_droid_codec_set_port_enabled (comp, comp->in_port->def.nPortIndex,
          TRUE)) {
    return FALSE;
  }

  if (!gst_droid_codec_set_port_enabled (comp, comp->out_port->def.nPortIndex,
          TRUE)) {
    return FALSE;
  }

  /* Let's allocate our buffers. */
  if (!gst_droid_codec_allocate_port_buffers (comp, comp->in_port, sink)) {
    return FALSE;
  }

  if (!gst_droid_codec_allocate_port_buffers (comp, comp->out_port, src)) {
    return FALSE;
  }

  if (!gst_droid_codec_wait_for_state (comp, OMX_StateIdle)) {
    GST_ERROR_OBJECT (comp->parent, "component failed to reach idle state");
    return FALSE;
  }

  GST_DEBUG_OBJECT (comp->parent, "component is in idle state");

  if (!gst_droid_codec_set_state (comp, OMX_StateExecuting)) {
    return FALSE;
  }

  if (!gst_droid_codec_wait_for_state (comp, OMX_StateExecuting)) {
    GST_ERROR_OBJECT (comp->parent,
        "component failed to reach executing state");
    return FALSE;
  }

  GST_DEBUG_OBJECT (comp->parent, "component in executing state");

  /* give back all output buffers */
  if (!gst_droid_codec_return_output_buffers (comp)) {
    GST_ERROR_OBJECT (comp->parent,
        "failed to hand output buffers to the codec");
    return FALSE;
  }

  g_mutex_lock (&comp->lock);
  comp->started = TRUE;
  g_mutex_unlock (&comp->lock);

  return TRUE;
}

static void
gst_droid_codec_pop_full_locked (GstDroidComponent * comp)
{
  OMX_BUFFERHEADERTYPE *buff;
  GstBuffer *buffer;

  while (comp->full->length > 0) {
    buff = g_queue_pop_head (comp->full);

    if (buff) {
      buffer = gst_omx_buffer_get_buffer (comp, buff);
      gst_buffer_unref (buffer);
    }
  }
}

void
gst_droid_codec_empty_full (GstDroidComponent * comp)
{
  g_mutex_lock (&comp->full_lock);
  gst_droid_codec_pop_full_locked (comp);
  g_mutex_unlock (&comp->full_lock);
}

void
gst_droid_codec_stop_component (GstDroidComponent * comp)
{
  GST_DEBUG_OBJECT (comp->parent, "stop");

  g_mutex_lock (&comp->lock);
  comp->started = FALSE;
  g_mutex_unlock (&comp->lock);

  gst_droid_codec_set_state (comp, OMX_StateIdle);

  if (!gst_droid_codec_wait_for_state (comp, OMX_StateIdle)) {
    GST_ERROR_OBJECT (comp->parent, "component failed to reach idle state");
  }

  gst_droid_codec_set_state (comp, OMX_StateLoaded);

  gst_buffer_pool_set_active (comp->in_port->buffers, FALSE);
  gst_buffer_pool_set_active (comp->out_port->buffers, FALSE);

  g_mutex_lock (&comp->full_lock);
  gst_droid_codec_pop_full_locked (comp);
  g_mutex_unlock (&comp->full_lock);

  if (!gst_droid_codec_wait_for_state (comp, OMX_StateLoaded)) {
    GST_ERROR_OBJECT (comp->parent, "component failed to reach loaded state");
  }

  /* TODO: flush our ports? */
  gst_droid_codec_set_port_enabled (comp, comp->in_port->def.nPortIndex, FALSE);
  gst_droid_codec_set_port_enabled (comp, comp->out_port->def.nPortIndex,
      FALSE);

  gst_object_unref (comp->in_port->buffers);
  comp->in_port->buffers = NULL;

  gst_object_unref (comp->out_port->buffers);
  comp->out_port->buffers = NULL;

  gst_object_unref (comp->in_port->allocator);
  comp->in_port->allocator = NULL;

  gst_object_unref (comp->out_port->allocator);
  comp->out_port->allocator = NULL;

  GST_INFO_OBJECT (comp->parent, "component is in loaded state");
}

static GstBuffer *
gst_droid_codec_acquire_buffer_from_pool (GstDroidComponent * comp,
    GstBufferPool * pool)
{
  GstBuffer *buffer = NULL;
  GstBufferPoolAcquireParams params;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (comp->parent, "acquire buffer from pool %p", pool);

  while (TRUE) {
    if (gst_droid_codec_has_error (comp)) {
      return NULL;
    }

    if (gst_droid_codec_needs_reconfigure (comp)) {
      return NULL;
    }

    if (!gst_droid_codec_is_running (comp)) {
      return NULL;
    }

    params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;

    ret = gst_buffer_pool_acquire_buffer (pool, &buffer, &params);
    if (buffer || ret != GST_FLOW_EOS) {
      break;
    } else if (ret == GST_FLOW_EOS) {
      GST_DEBUG_OBJECT (comp->parent, "waiting for buffers");
      usleep (WAIT_TIMEOUT);
    }
  }

  return buffer;
}

static void
gst_droid_codec_release_buffer (DroidBufferCallbackMapInfo *data)
{
  GST_DEBUG ("release buffer");
  gst_buffer_unmap (data->buffer, &data->info);
  gst_buffer_unref (data->buffer);

  /* We need to release the input buffer */
  gst_buffer_unref (data->frame->input_buffer);
  data->frame->input_buffer = NULL;

  gst_video_codec_frame_unref (data->frame);
  g_slice_free(DroidBufferCallbackMapInfo, data);
}

gboolean
gst_droid_codec_consume_frame (DroidMediaCodec * codec,
    GstVideoCodecFrame * frame, GstClockTime ts)
{
  DroidMediaCodecData data;
  GstMapInfo info;
  DroidMediaBufferCallbacks cb;
  DroidBufferCallbackMapInfo *buffer_data;

  GST_DEBUG ("consume frame");

  data.sync = GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame) ? true : false;
  data.ts = GST_TIME_AS_USECONDS(ts);

  if (!gst_buffer_map (frame->input_buffer, &info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return FALSE;
  }

  data.size = info.size;
  data.data = info.data;

  GST_LOG ("Consuming frame of size %d", data.size);

  buffer_data = g_slice_new (DroidBufferCallbackMapInfo);

  cb.unref = gst_droid_codec_release_buffer;
  cb.data = buffer_data;

  buffer_data->buffer = gst_buffer_ref (frame->input_buffer);
  buffer_data->info = info;
  buffer_data->frame = frame; /* We have a ref already */

  droid_media_codec_write (codec, &data, &cb);

  GST_DEBUG ("frame consumed");

  return TRUE;
}

gboolean
gst_droid_codec_set_codec_data (GstDroidComponent * comp,
    GstBuffer * codec_data)
{
  GstBuffer *buf;
  OMX_BUFFERHEADERTYPE *omx_buf;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (comp->parent, "set codec_data");

  buf = gst_droid_codec_acquire_buffer_from_pool (comp, comp->in_port->buffers);
  if (!buf && gst_droid_codec_has_error (comp)) {
    GST_INFO_OBJECT (comp->parent, "component in error state");
    return FALSE;
  } else if (!buf) {
    GST_ERROR_OBJECT (comp->parent, "could not acquire buffer");
    return FALSE;
  }

  /* get omx buffer */
  omx_buf =
      gst_droid_codec_omx_allocator_get_omx_buffer (gst_buffer_peek_memory (buf,
          0));
  if (!omx_buf) {
    gst_buffer_unref (buf);

    GST_ERROR_OBJECT (comp->parent, "failed to get omx buffer");
    return FALSE;
  }

  if (omx_buf->nAllocLen - omx_buf->nOffset < gst_buffer_get_size (codec_data)) {
    gst_buffer_unref (buf);
    GST_ERROR_OBJECT (comp->parent, "codec config is too large");
    return FALSE;
  }

  omx_buf->nFilledLen = gst_buffer_get_size (codec_data);
  gst_buffer_extract (codec_data, 0,
      omx_buf->pBuffer + omx_buf->nOffset, omx_buf->nFilledLen);

  gst_buffer_unref (buf);

  omx_buf->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
  omx_buf->nTimeStamp = 0;
  omx_buf->nTickCount = 0;
  omx_buf->pAppPrivate = NULL;

  err = OMX_EmptyThisBuffer (comp->omx, omx_buf);

  if (err != OMX_ErrorNone) {
    GST_ERROR ("got error %s (0x%08x) while calling EmptyThisBuffer",
        gst_omx_error_to_string (err), err);

    return FALSE;
  }

  return TRUE;
}

GstBuffer *
gst_omx_buffer_get_buffer (GstDroidComponent * comp,
    OMX_BUFFERHEADERTYPE * buff)
{
  GST_DEBUG_OBJECT (comp->parent, "get buffer");

  return (GstBuffer *) buff->pAppPrivate;
}

gboolean
gst_droid_codec_return_output_buffers (GstDroidComponent * comp)
{
  GstBuffer *buffer;
  GstBufferPoolAcquireParams params;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (comp->parent, "return output buffers");

  params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
  while (gst_buffer_pool_acquire_buffer (comp->out_port->buffers, &buffer,
          &params) == GST_FLOW_OK) {
    OMX_BUFFERHEADERTYPE *omx;

    if (comp->out_port->usage != -1) {
      GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
      if (!gst_is_gralloc_memory (mem)) {
        GstMemory *gralloc =
            gst_droid_codec_gralloc_allocator_get_gralloc_memory (mem);
        if (!gralloc) {
          // TODO: error
        }

        gst_memory_ref (gralloc);

        gst_buffer_insert_memory (buffer, 0, gralloc);
        GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);
      }

      omx =
          gst_droid_codec_gralloc_allocator_get_omx_buffer
          (gst_buffer_peek_memory (buffer, 1));
    } else {
      omx =
          gst_droid_codec_omx_allocator_get_omx_buffer (gst_buffer_peek_memory
          (buffer, 0));
    }

    /* reset buffer */
    omx->nFilledLen = 0;
    omx->nOffset = 0;
    omx->nFlags = 0;
    omx->pAppPrivate = buffer;

    GST_DEBUG_OBJECT (comp->parent, "handing buffer %p to the omx codec",
        buffer);

    err = OMX_FillThisBuffer (comp->omx, omx);

    if (err != OMX_ErrorNone) {
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (comp->parent, "returned output buffers");

  return TRUE;
}

void
gst_droid_codec_unset_needs_reconfigure (GstDroidComponent * comp)
{
  g_mutex_lock (&comp->lock);
  comp->needs_reconfigure = FALSE;
  g_mutex_unlock (&comp->lock);
}

gboolean
gst_droid_codec_needs_reconfigure (GstDroidComponent * comp)
{
  gboolean reconfigure;

  g_mutex_lock (&comp->lock);
  reconfigure = comp->needs_reconfigure;
  g_mutex_unlock (&comp->lock);

  return reconfigure;
}

gboolean
gst_droid_codec_has_error (GstDroidComponent * comp)
{
  gboolean err;

  g_mutex_lock (&comp->lock);
  err = comp->error;
  g_mutex_unlock (&comp->lock);

  return err;
}

gboolean
gst_droid_codec_is_running (GstDroidComponent * comp)
{
  gboolean started;
  g_mutex_lock (&comp->lock);
  started = comp->started;
  g_mutex_unlock (&comp->lock);

  return started;
}

void
gst_droid_codec_set_running (GstDroidComponent * comp, gboolean running)
{
  g_mutex_lock (&comp->lock);
  comp->started = running;
  g_mutex_unlock (&comp->lock);
}

gboolean
gst_droid_codec_reconfigure_output_port (GstDroidComponent * comp)
{
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (comp->parent, "reconfigure output port");

  /* disable port */
  if (!gst_droid_codec_set_port_enabled (comp, comp->out_port->def.nPortIndex,
          FALSE)) {
    return FALSE;
  }

  g_mutex_lock (&comp->full_lock);
  gst_droid_codec_pop_full_locked (comp);
  g_mutex_unlock (&comp->full_lock);

  /* free buffers */
  gst_buffer_pool_set_active (comp->out_port->buffers, FALSE);

  /* enable port */
  if (!gst_droid_codec_set_port_enabled (comp, comp->out_port->def.nPortIndex,
          TRUE)) {
    return FALSE;
  }

  /* update port definition */
  err =
      gst_droid_codec_get_param (comp, OMX_IndexParamPortDefinition,
      &comp->out_port->def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) getting output port definition",
        gst_omx_error_to_string (err), err);

    return FALSE;
  }

  return TRUE;
}

gboolean
gst_droid_codec_flush (GstDroidComponent * comp, gboolean pause)
{
  OMX_ERRORTYPE err;
  GST_DEBUG_OBJECT (comp->parent, "flush %d", pause);

  if (pause) {
    g_mutex_lock (&comp->lock);
    comp->started = FALSE;
    g_mutex_unlock (&comp->lock);

    /* set state to pause */
    if (!gst_droid_codec_set_state (comp, OMX_StatePause)) {
      return FALSE;
    }

    if (!gst_droid_codec_wait_for_state (comp, OMX_StatePause)) {
      GST_ERROR_OBJECT (comp->parent, "component failed to reach pause state");
      return FALSE;
    }

    /* flush ports */
    err = OMX_SendCommand (comp->omx, OMX_CommandFlush, -1, NULL);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (comp->parent,
          "got error %s (0x%08x) while flushing ports",
          gst_omx_error_to_string (err), err);
      return FALSE;
    }
  } else {
    /* set state to executing */
    if (!gst_droid_codec_set_state (comp, OMX_StateExecuting)) {
      return FALSE;
    }

    if (!gst_droid_codec_wait_for_state (comp, OMX_StateExecuting)) {
      GST_ERROR_OBJECT (comp->parent,
          "component failed to reach executing state");
      return FALSE;
    }

    /* fill port */
    if (!gst_droid_codec_return_output_buffers (comp)) {
      GST_ERROR_OBJECT (comp->parent,
          "failed to hand output buffers to the codec");
      return FALSE;
    }

    g_mutex_lock (&comp->lock);
    comp->started = TRUE;
    g_mutex_unlock (&comp->lock);
  }

  return TRUE;
}

static gboolean
gst_droid_codec_get_encoder_profile_level (GstDroidComponent * comp,
    GstCaps * caps, const CodecProfileLevel defaultProfileLevel,
    CodecProfileLevel user, CodecProfileLevel * profileLevel, int port)
{
  /* stolen from android OMXCodec.cpp */
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  OMX_ERRORTYPE err;

  if (user.mProfile == -1) {
    user.mProfile = defaultProfileLevel.mProfile;
  }

  if (user.mLevel == -1) {
    user.mLevel = defaultProfileLevel.mLevel;
  }

  GST_DEBUG_OBJECT (comp->parent, "requested profile: 0x%lx, level: 0x%lx",
      user.mProfile, user.mLevel);

  GST_OMX_INIT_STRUCT (&param);

  param.nPortIndex = port;

  /* Are the target profile and level supported by the encoder? */
  for (param.nProfileIndex = 0;; ++param.nProfileIndex) {
    int supportedProfile, supportedLevel;

    err =
        gst_droid_codec_get_param (comp,
        OMX_IndexParamVideoProfileLevelQuerySupported, &param);

    if (err != OMX_ErrorNone) {
      break;
    }

    supportedProfile = param.eProfile;
    supportedLevel = param.eLevel;
    GST_DEBUG_OBJECT (comp->parent, "supported profile: 0x%x, level 0x%x",
        supportedProfile, supportedLevel);

    if (user.mProfile == supportedProfile && user.mLevel <= supportedLevel) {
      profileLevel->mProfile = user.mProfile;
      profileLevel->mLevel = user.mLevel;
      return TRUE;
    }
  }

  GST_ERROR_OBJECT (comp->parent,
      "profile (0x%lx) and level (0x%lx) are not supported", user.mProfile,
      user.mLevel);

  return FALSE;
}

static gboolean
gst_droid_codec_apply_mpeg4video_encoding_params (GstDroidComponent * comp,
    GstCaps * caps, int fps)
{
  /* stolen from android OMXCodec.cpp */

  OMX_VIDEO_PARAM_MPEG4TYPE mpeg4type;
  OMX_ERRORTYPE err;
  CodecProfileLevel defaultProfileLevel, profileLevel, user;
  GstStructure *s;

  GST_DEBUG_OBJECT (comp->parent, "apply mpeg4 video encoding params");

  GST_OMX_INIT_STRUCT (&mpeg4type);
  mpeg4type.nPortIndex = comp->out_port->def.nPortIndex;
  err = gst_droid_codec_get_param (comp, OMX_IndexParamVideoMpeg4, &mpeg4type);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) getting OMX_IndexParamVideoMpeg4",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  mpeg4type.nSliceHeaderSpacing = 0;
  mpeg4type.bSVH = OMX_FALSE;
  mpeg4type.bGov = OMX_FALSE;
  mpeg4type.nAllowedPictureTypes =
      OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP;
  mpeg4type.nPFrames = fps - 1;

  if (mpeg4type.nPFrames == 0) {
    mpeg4type.nAllowedPictureTypes = OMX_VIDEO_PictureTypeI;
  }

  mpeg4type.nBFrames = 0;
  mpeg4type.nIDCVLCThreshold = 0;
  mpeg4type.bACPred = OMX_TRUE;
  mpeg4type.nMaxPacketSize = 256;
  mpeg4type.nTimeIncRes = 1000;
  mpeg4type.nHeaderExtension = 0;
  mpeg4type.bReversibleVLC = OMX_FALSE;

  defaultProfileLevel.mProfile = mpeg4type.eProfile;
  defaultProfileLevel.mLevel = mpeg4type.eLevel;

  s = gst_caps_get_structure (caps, 0);
  user.mProfile =
      gst_encoder_params_get_mpeg4_profile (gst_structure_get_string (s,
          "profile"));
  user.mLevel =
      gst_encoder_params_get_mpeg4_level (gst_structure_get_string (s,
          "level"));

  if (!gst_droid_codec_get_encoder_profile_level (comp, caps,
          defaultProfileLevel, user, &profileLevel,
          comp->out_port->def.nPortIndex)) {
    return FALSE;
  }

  mpeg4type.eProfile = profileLevel.mProfile;
  mpeg4type.eLevel = profileLevel.mLevel;

  err = gst_droid_codec_set_param (comp, OMX_IndexParamVideoMpeg4, &mpeg4type);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) setting OMX_IndexParamVideoMpeg4",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_droid_codec_apply_avc_encoding_params (GstDroidComponent * comp,
    GstCaps * caps, int fps)
{
  /* stolen from android OMXCodec.cpp */

  OMX_VIDEO_PARAM_AVCTYPE h264type;
  OMX_ERRORTYPE err;
  CodecProfileLevel defaultProfileLevel, profileLevel, user;
  GstStructure *s;

  GST_DEBUG_OBJECT (comp->parent, "apply avc encoding params");

  GST_OMX_INIT_STRUCT (&h264type);

  h264type.nPortIndex = comp->out_port->def.nPortIndex;

  err = gst_droid_codec_get_param (comp, OMX_IndexParamVideoAvc, &h264type);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) getting OMX_IndexParamVideoAvc",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  h264type.nAllowedPictureTypes =
      OMX_VIDEO_PictureTypeI | OMX_VIDEO_PictureTypeP;

  defaultProfileLevel.mProfile = h264type.eProfile;
  defaultProfileLevel.mLevel = h264type.eLevel;

  s = gst_caps_get_structure (caps, 0);
  user.mProfile =
      gst_encoder_params_get_avc_profile (gst_structure_get_string (s,
          "profile"));
  user.mLevel =
      gst_encoder_params_get_avc_level (gst_structure_get_string (s, "level"));

  if (!gst_droid_codec_get_encoder_profile_level (comp, caps,
          defaultProfileLevel, user, &profileLevel,
          comp->out_port->def.nPortIndex)) {
    return FALSE;
  }

  h264type.eProfile = profileLevel.mProfile;
  h264type.eLevel = profileLevel.mLevel;

  if (h264type.eProfile != OMX_VIDEO_AVCProfileBaseline) {
    GST_WARNING_OBJECT (comp->parent,
        "Use baseline profile instead of 0x%x for AVC recording",
        h264type.eProfile);

    h264type.eProfile = OMX_VIDEO_AVCProfileBaseline;
  }

  if (h264type.eProfile == OMX_VIDEO_AVCProfileBaseline) {
    h264type.nSliceHeaderSpacing = 0;
    h264type.bUseHadamard = OMX_TRUE;
    h264type.nRefFrames = 1;
    h264type.nBFrames = 0;
    h264type.nPFrames = fps - 1;

    if (h264type.nPFrames == 0) {
      h264type.nAllowedPictureTypes = OMX_VIDEO_PictureTypeI;
    }

    h264type.nRefIdx10ActiveMinus1 = 0;
    h264type.nRefIdx11ActiveMinus1 = 0;
    h264type.bEntropyCodingCABAC = OMX_FALSE;
    h264type.bWeightedPPrediction = OMX_FALSE;
    h264type.bconstIpred = OMX_FALSE;
    h264type.bDirect8x8Inference = OMX_FALSE;
    h264type.bDirectSpatialTemporal = OMX_FALSE;
    h264type.nCabacInitIdc = 0;
  }

  if (h264type.nBFrames != 0) {
    h264type.nAllowedPictureTypes |= OMX_VIDEO_PictureTypeB;
  }

  h264type.bEnableUEP = OMX_FALSE;
  h264type.bEnableFMO = OMX_FALSE;
  h264type.bEnableASO = OMX_FALSE;
  h264type.bEnableRS = OMX_FALSE;
  h264type.bFrameMBsOnly = OMX_TRUE;
  h264type.bMBAFF = OMX_FALSE;
  h264type.eLoopFilterMode = OMX_VIDEO_AVCLoopFilterEnable;

  err = gst_droid_codec_set_param (comp, OMX_IndexParamVideoAvc, &h264type);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) setting OMX_IndexParamVideoAvc",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_droid_codec_apply_bitrate (GstDroidComponent * comp, int bitrate, int port)
{
  OMX_VIDEO_PARAM_BITRATETYPE bitrateType;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (comp->parent, "apply bitrate %i", bitrate);

#if 0
  if (bitrate == GST_DROID_ENC_TARGET_BITRATE_DEFAULT) {
    GST_INFO_OBJECT (comp->parent, "bitrate is the default");
    return TRUE;
  }
#endif

  GST_OMX_INIT_STRUCT (&bitrateType);

  bitrateType.nPortIndex = port;

  err =
      gst_droid_codec_get_param (comp, OMX_IndexParamVideoBitrate,
      &bitrateType);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) getting OMX_IndexParamVideoBitrate",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  bitrateType.eControlRate = OMX_Video_ControlRateVariable;
  bitrateType.nTargetBitrate = bitrate;

  err =
      gst_droid_codec_set_param (comp, OMX_IndexParamVideoBitrate,
      &bitrateType);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (comp->parent,
        "got error %s (0x%08x) setting OMX_IndexParamVideoBitrate",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_droid_codec_apply_encoding_params (GstDroidComponent * comp,
    GstVideoInfo * info, GstCaps * caps, int bitrate)
{
  gboolean ret;
  int fps;

  GST_DEBUG_OBJECT (comp->parent, "apply encoding params");

  fps = info->fps_n / info->fps_d;

  if (!g_strcmp0 (comp->handle->type, GST_DROID_CODEC_TYPE_MPEG4VIDEO_ENC)) {
    ret = gst_droid_codec_apply_mpeg4video_encoding_params (comp, caps, fps);
  } else if (!g_strcmp0 (comp->handle->type, GST_DROID_CODEC_TYPE_AVC_ENC)) {
    ret = gst_droid_codec_apply_avc_encoding_params (comp, caps, fps);
  } else {
    GST_ERROR_OBJECT ("unknown encoder type %s", comp->handle->type);
    ret = FALSE;
  }

  if (ret) {
    /* setup error correction (optional) */
    OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE errorCorrectionType;
    OMX_ERRORTYPE err;

    GST_OMX_INIT_STRUCT (&errorCorrectionType);
    errorCorrectionType.nPortIndex = comp->out_port->def.nPortIndex;

    err =
        gst_droid_codec_get_param (comp, OMX_IndexParamVideoErrorCorrection,
        &errorCorrectionType);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (comp->parent,
          "got error %s (0x%08x) getting OMX_IndexParamVideoErrorCorrection",
          gst_omx_error_to_string (err), err);
    } else {
      errorCorrectionType.bEnableHEC = OMX_FALSE;
      errorCorrectionType.bEnableResync = OMX_TRUE;
      errorCorrectionType.nResynchMarkerSpacing = 256;
      errorCorrectionType.bEnableDataPartitioning = OMX_FALSE;
      errorCorrectionType.bEnableRVLC = OMX_FALSE;

      err = gst_droid_codec_set_param (comp, OMX_IndexParamVideoErrorCorrection,
          &errorCorrectionType);
      if (err != OMX_ErrorNone) {
        GST_WARNING_OBJECT (comp->parent,
            "got error %s (0x%08x) setting OMX_IndexParamVideoErrorCorrection",
            gst_omx_error_to_string (err), err);
      }
    }
  }

  if (ret) {
    return gst_droid_codec_apply_bitrate (comp, bitrate,
        comp->out_port->def.nPortIndex);
  }

  return ret;
}

void
gst_droid_codec_timestamp (GstBuffer * buffer, OMX_BUFFERHEADERTYPE * buff)
{
  GST_BUFFER_PTS (buffer) =
      gst_util_uint64_scale (buff->nTimeStamp, GST_SECOND,
      OMX_TICKS_PER_SECOND);

  if (buff->nTickCount != 0) {
    GST_BUFFER_DURATION (buffer) =
        gst_util_uint64_scale (buff->nTickCount, GST_SECOND,
        OMX_TICKS_PER_SECOND);
  }

}
