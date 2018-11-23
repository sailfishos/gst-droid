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

#include "gstdroidvdec.h"
#include "gst/droid/gstdroidmediabuffer.h"
#include "gst/droid/gstdroidbufferpool.h"
#include "plugin.h"
#include "droidmediaconstants.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <string.h>             /* memset() */
#ifdef HAVE_ORC
#include <orc/orc.h>
#else
#define orc_memcpy memcpy
#endif

#define GST_DROID_DEC_NUM_BUFFERS         2

#define gst_droidvdec_parent_class parent_class
G_DEFINE_TYPE (GstDroidVDec, gst_droidvdec, GST_TYPE_VIDEO_DECODER);

GST_DEBUG_CATEGORY_EXTERN (gst_droid_vdec_debug);
#define GST_CAT_DEFAULT gst_droid_vdec_debug

typedef struct
{
  int *hal_format;
  GstVideoFormat gst_format;
  GstDroidVideoConvertToI420 convert_to_i420;
  gsize bytes_per_pixel;
  gsize h_align;
  gsize v_align;

} GstDroidVideoFormatMap;

static GstStaticPadTemplate gst_droidvdec_src_template_factory =
    GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SRC_NAME,
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_BUFFER,
            GST_DROID_MEDIA_BUFFER_MEMORY_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE ("I420")));

static gboolean gst_droidvdec_configure_state (GstVideoDecoder * decoder,
    guint width, guint height);
static void gst_droidvdec_error (void *data, int err);
static int gst_droidvdec_size_changed (void *data, int32_t width,
    int32_t height);
static void gst_droidvdec_signal_eos (void *data);
static void gst_droidvdec_buffers_released (void *user);
static void gst_droidvdec_frame_available (void *user);
static void gst_droidvdec_data_available (void *data,
    DroidMediaCodecData * encoded);
static gboolean gst_droidvdec_convert_buffer (GstDroidVDec * dec,
    GstBuffer * out, DroidMediaData * in, GstVideoInfo * info);
static void gst_droidvdec_loop (GstDroidVDec * dec);
static GstFlowReturn gst_droidvdec_finish_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

static void
gst_droidvdec_loop (GstDroidVDec * dec)
{
  GST_LOG_OBJECT (dec, "loop");

  if (!gst_droid_buffer_pool_wait_for_buffer (dec->pool)) {
    goto out;
  }

  while (droid_media_codec_loop (dec->codec)
      == DROID_MEDIA_CODEC_LOOP_OK) {
    GST_LOG_OBJECT (dec, "tick");
    return;
  }

out:
  GST_INFO_OBJECT (dec, "pausing task");
  gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (GST_VIDEO_DECODER (dec)));
}


static void
gst_droidvec_copy_plane (guint8 * out, gint stride_out, guint8 * in,
    gint stride_in, gint width, gint height)
{
  int i;
  for (i = 0; i < height; i++) {
    orc_memcpy (out, in, width);
    out += stride_out;
    in += stride_in;
  }
}

static void
gst_droidvec_copy_packed_planes (guint8 * out0, guint8 * out1, gint stride_out,
    guint8 * in, gint stride_in, gint width, gint height)
{
  int x, y;
  for (y = 0; y < height; y++) {
    guint8 *row = in;
    for (x = 0; x < width; x++) {
      out0[x] = row[0];
      out1[x] = row[1];
      row += 2;
    }

    out0 += stride_out;
    out1 += stride_out;
    in += stride_in;
  }
}

#define ALIGN_SIZE(size, to) (((size) + to  - 1) & ~(to - 1))

static gboolean
gst_droidvdec_convert_native_to_i420 (GstDroidVDec * dec, GstMapInfo * out,
    DroidMediaData * in, GstVideoInfo * info, gsize width, gsize height)
{
  gsize size = width * height * 3 / 2;
  gboolean use_external_buffer = out->size != size;
  guint8 *data = NULL;
  gboolean ret = TRUE;

  if (use_external_buffer) {
    GST_DEBUG_OBJECT (dec, "using an external buffer for I420 conversion.");
    data = g_malloc (size);
  } else {
    data = out->data;
  }

  if (droid_media_convert_to_i420 (dec->convert, in, data) != true) {
    GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, (NULL),
        ("failed to convert frame"));

    ret = FALSE;
  } else if (use_external_buffer) {
    /* fix up the buffer */
    /* Code is based on gst-colorconv qcom backend */

    gint stride = GST_VIDEO_INFO_COMP_STRIDE (info, 0);
    gint strideUV = GST_VIDEO_INFO_COMP_STRIDE (info, 1);
    guint8 *p = data;
    guint8 *dst = out->data;
    int i;
    int x;

    /* Y */
    for (i = 0; i < info->height; i++) {
      orc_memcpy (dst, p, info->width);
      dst += stride;
      p += width;
    }

    /* NOP if height == info->height */
    p += (height - info->height) * width;
    /* U and V */
    for (x = 0; x < 2; x++) {
      for (i = 0; i < info->height / 2; i++) {
        orc_memcpy (dst, p, info->width / 2);
        dst += strideUV;
        p += width / 2;
      }

      /* NOP if height == info->height */
      p += (height - info->height) / 2 * width / 2;
    }
  }

  if (use_external_buffer && data) {
    g_free (data);
  }

  return ret;
}

static gboolean
gst_droidvdec_convert_yuv420_planar_to_i420 (GstDroidVDec * dec,
    GstMapInfo * out, DroidMediaData * in, GstVideoInfo * info, gsize width,
    gsize height)
{
  /* Buffer is already I420, so we can copy it straight over */
  /* though we need to handle the cropping */

  GST_DEBUG_OBJECT (dec, "Copying I420 buffer");
  gint top = dec->crop_rect.top;
  gint left = dec->crop_rect.left;
  gint crop_width = dec->crop_rect.right - left;
  gint crop_height = dec->crop_rect.bottom - top;

  guint8 *y = in->data + (top * width) + left;
  guint8 *u = in->data + (width * height) + (top * width / 2) + (left / 2);
  guint8 *v =
      in->data + (width * height) + (width * height / 4) +
      (top * width / 2) + (left / 2);

  gst_droidvec_copy_plane (out->data + info->offset[0],
      info->stride[0], y, width, crop_width, crop_height);
  gst_droidvec_copy_plane (out->data + info->offset[1],
      info->stride[1], u, width / 2, crop_width / 2, crop_height / 2);
  gst_droidvec_copy_plane (out->data + info->offset[2],
      info->stride[2], v, width / 2, crop_width / 2, crop_height / 2);

  return TRUE;
}

static gboolean
gst_droidvdec_convert_yuv420_semi_planar_to_i420 (GstDroidVDec * dec,
    GstMapInfo * out, DroidMediaData * in, GstVideoInfo * info, gsize width,
    gsize height)
{
  GST_DEBUG_OBJECT (dec, "Converting from OMX_COLOR_FormatYUV420SemiPlanar");
  gint stride = width;
  gint slice_height = ALIGN_SIZE (height, 16);
  gint top = dec->crop_rect.top;
  gint left = dec->crop_rect.left;

  guint8 *y = in->data + (top * stride) + left;
  guint8 *uv = in->data + (stride * slice_height) + (top * stride / 2) + left;

  gst_droidvec_copy_plane (out->data + info->offset[0],
      info->stride[0], y, stride, info->width, info->height);
  gst_droidvec_copy_packed_planes (out->data + info->offset[1],
      out->data + info->offset[2], info->stride[1], uv, stride,
      info->width / 2, info->height / 2);

  return TRUE;
}

static gboolean
gst_droidvdec_convert_yuv420_packed_semi_planar_to_i420 (GstDroidVDec * dec,
    GstMapInfo * out, DroidMediaData * in, GstVideoInfo * info, gsize width,
    gsize height)
{
  /* copy to the output buffer swapping the u and v planes and cropping if necessary */
  /* NV12 format with 128 byte alignment */
  GST_DEBUG_OBJECT (dec, "Converting from qcom NV12 semi planar");
  gint stride = ALIGN_SIZE (width, 128);
  gint slice_height = ALIGN_SIZE (height, 32);
  gint top = ALIGN_SIZE (dec->crop_rect.top, 2);
  gint left = ALIGN_SIZE (dec->crop_rect.left, 2);

  guint8 *y = in->data + (top * stride) + left;
  guint8 *uv = in->data + (stride * slice_height) + (top * stride / 2) + left;

  gst_droidvec_copy_plane (out->data + info->offset[0],
      info->stride[0], y, stride, info->width, info->height);
  gst_droidvec_copy_packed_planes (out->data + info->offset[1],
      out->data + info->offset[2], info->stride[1], uv, stride,
      info->width / 2, info->height / 2);

  return TRUE;
}

static gboolean
gst_droidvdec_create_codec (GstDroidVDec * dec, GstBuffer * input)
{
  DroidMediaCodecDecoderMetaData md;
  const gchar *droid = gst_droid_codec_get_droid_type (dec->codec_type);

  GST_INFO_OBJECT (dec, "create codec of type %s: %dx%d",
      droid, dec->in_state->info.width, dec->in_state->info.height);

  memset (&md, 0x0, sizeof (md));

  /* Let's take care of the buffer pool first */
  if (!dec->pool) {
    GstStructure *config;

    dec->pool = gst_droid_buffer_pool_new ();
    config = gst_buffer_pool_get_config (dec->pool);
    /* pass NULL for the caps. We don't have it yet */
    gst_buffer_pool_config_set_params (config, NULL, 0,
        GST_DROID_DEC_NUM_BUFFERS, GST_DROID_DEC_NUM_BUFFERS);

    if (!gst_buffer_pool_set_config (dec->pool, config)) {
      GST_ELEMENT_ERROR (dec, STREAM, FAILED, (NULL),
          ("Failed to configure buffer pool"));
      return FALSE;
    }

    if (!gst_buffer_pool_set_active (dec->pool, TRUE)) {
      GST_ELEMENT_ERROR (dec, STREAM, FAILED, (NULL),
          ("Failed to activate buffer pool"));
      return FALSE;
    }
  } else {
    gst_buffer_pool_set_flushing (dec->pool, FALSE);
  }

  md.parent.type = droid;
  md.parent.width = dec->in_state->info.width;
  md.parent.height = dec->in_state->info.height;
  md.parent.fps = dec->in_state->info.fps_n / dec->in_state->info.fps_d;
  md.parent.flags =
      DROID_MEDIA_CODEC_HW_ONLY | DROID_MEDIA_CODEC_USE_EXTERNAL_LOOP;
  md.codec_data.size = 0;

  if (!dec->use_hardware_buffers) {
    md.parent.flags |= DROID_MEDIA_CODEC_NO_MEDIA_BUFFER;
  }

  switch (gst_droid_codec_create_decoder_codec_data (dec->codec_type,
          dec->codec_data, &md.codec_data, input)) {
    case GST_DROID_CODEC_CODEC_DATA_OK:
      break;

    case GST_DROID_CODEC_CODEC_DATA_NOT_NEEDED:
      g_assert (dec->codec_data == NULL);
      break;

    case GST_DROID_CODEC_CODEC_DATA_ERROR:
      GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
          ("Failed to create codec_data."));
      goto error;
  }

  dec->codec = droid_media_codec_create_decoder (&md);

  if (md.codec_data.size > 0) {
    g_free (md.codec_data.data);
  }

  if (!dec->codec) {
    GST_ELEMENT_ERROR (dec, LIBRARY, SETTINGS, NULL,
        ("Failed to create decoder"));

    goto error;
  }

  dec->queue = droid_media_codec_get_buffer_queue (dec->codec);

  {
    DroidMediaCodecCallbacks cb;
    cb.signal_eos = gst_droidvdec_signal_eos;
    cb.error = gst_droidvdec_error;
    cb.size_changed = gst_droidvdec_size_changed;
    droid_media_codec_set_callbacks (dec->codec, &cb, dec);
  }

  if (dec->queue) {
    DroidMediaBufferQueueCallbacks cb;
    cb.buffers_released = gst_droidvdec_buffers_released;
    cb.frame_available = gst_droidvdec_frame_available;
    droid_media_buffer_queue_set_callbacks (dec->queue, &cb, dec);
  } else {
    DroidMediaCodecDataCallbacks cb;
    cb.data_available = gst_droidvdec_data_available;
    droid_media_codec_set_data_callbacks (dec->codec, &cb, dec);
  }

  if (!droid_media_codec_start (dec->codec)) {
    GST_ELEMENT_ERROR (dec, LIBRARY, INIT, (NULL),
        ("Failed to start the decoder"));

    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;
    dec->queue = NULL;

    goto error;
  }

  /* now start our task */
  GST_LOG_OBJECT (dec, "starting task");

  gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (GST_VIDEO_DECODER (dec)),
      (GstTaskFunction) gst_droidvdec_loop, gst_object_ref (dec),
      gst_object_unref);
  return TRUE;

error:
  gst_buffer_pool_set_active (dec->pool, FALSE);
  return FALSE;
}

static void
gst_droidvdec_buffers_released (G_GNUC_UNUSED void *user)
{
  GST_FIXME ("Not sure what to do here really");
}

static gboolean
gst_droidvdec_convert_buffer (GstDroidVDec * dec,
    GstBuffer * out, DroidMediaData * in, GstVideoInfo * info)
{
  gsize height = info->height;
  gsize width = info->width;
  gboolean ret;
  GstMapInfo map_info;

  GST_DEBUG_OBJECT (dec, "convert buffer");

  if (dec->codec_type->quirks & USE_CODEC_SUPPLIED_WIDTH_VALUE) {
    width = dec->codec_reported_width;
    GST_INFO_OBJECT (dec, "using codec supplied width %d", width);
  }

  if (dec->codec_type->quirks & USE_CODEC_SUPPLIED_HEIGHT_VALUE) {
    height = dec->codec_reported_height;
    GST_INFO_OBJECT (dec, "using codec supplied height %d", height);
  }

  if (!dec->convert_to_i420) {
    GST_ERROR_OBJECT (dec, "no i420 conversion function");
    ret = FALSE;
  } else if (!gst_buffer_map (out, &map_info, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (dec, "failed to map buffer");
    ret = FALSE;
  } else {
    ret = dec->convert_to_i420 (dec, &map_info, in, info, width, height);

    gst_buffer_unmap (out, &map_info);
  }

  return ret;
}

static void
gst_droidvdec_frame_available (void *user)
{
  GstDroidVDec *dec = (GstDroidVDec *) user;
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
  GstMemory *mem;
  guint width, height;
  GstVideoCodecFrame *frame;
  DroidMediaBuffer *buffer;
  GstBuffer *buff;
  GstVideoCropMeta *crop_meta;
  DroidMediaBufferCallbacks cb;
  DroidMediaBufferInfo droid_info;
  GstVideoInfo video_info;
  GstFlowReturn flow_ret;

  GST_DEBUG_OBJECT (dec, "frame available");

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (dec->dirty) {
    goto acquire_and_release;
  }

  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (dec, "not handling frame in error state: %s",
        gst_flow_get_name (dec->downstream_flow_ret));
    goto acquire_and_release;
  }

  flow_ret = gst_buffer_pool_acquire_buffer (dec->pool, &buff, NULL);

  if (flow_ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (dec, "failed to acquire buffer from pool: %s",
        gst_flow_get_name (flow_ret));
    goto acquire_and_release;
  }

  cb.ref = (DroidMediaCallback) gst_buffer_ref;
  cb.unref = (DroidMediaCallback) gst_buffer_unref;
  cb.data = buff;

  mem =
      gst_droid_media_buffer_allocator_alloc (dec->allocator, dec->queue, &cb);

  if (!mem) {
    /* TODO: what should we do here? */
    GST_ERROR_OBJECT (dec, "failed to acquire buffer from droidmedia");
    gst_buffer_unref (buff);
    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    return;
  }

  buffer = gst_droid_media_buffer_memory_get_buffer (mem);
  droid_media_buffer_get_info (buffer, &droid_info);

  gst_buffer_insert_memory (buff, 0, mem);

  /* Now we can configure the state */
  if (G_UNLIKELY (!dec->out_state)) {
    if (!gst_droidvdec_configure_state (decoder, droid_info.width,
            droid_info.height)) {
      dec->downstream_flow_ret = GST_FLOW_ERROR;
      gst_buffer_unref (buff);
      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
      return;
    }
  }

  /* we don't want to access the memory afterwards */
  mem = NULL;
  buffer = NULL;

  if (dec->bytes_per_pixel != 0) {
    width = ALIGN_SIZE (droid_info.stride, dec->h_align) / dec->bytes_per_pixel;
    height = ALIGN_SIZE (droid_info.height, dec->v_align);
  } else {
    width = droid_info.width;
    height = droid_info.height;
  }

  gst_video_info_set_format (&video_info, dec->format, width, height);


  crop_meta = gst_buffer_add_video_crop_meta (buff);
  crop_meta->x = droid_info.crop_rect.left;
  crop_meta->y = droid_info.crop_rect.top;
  crop_meta->width = droid_info.crop_rect.right - droid_info.crop_rect.left;
  crop_meta->height = droid_info.crop_rect.bottom - droid_info.crop_rect.top;

  GST_LOG_OBJECT (dec, "crop info: x=%d, y=%d, w=%d, h=%d", crop_meta->x,
      crop_meta->y, crop_meta->width, crop_meta->height);

  gst_buffer_add_video_meta_full (buff, GST_VIDEO_FRAME_FLAG_NONE, dec->format,
      droid_info.width, droid_info.height, video_info.finfo->n_planes,
      video_info.offset, video_info.stride);

  frame = gst_video_decoder_get_oldest_frame (decoder);

  if (G_UNLIKELY (!frame)) {
    /* TODO: what should we do here? */
    GST_WARNING_OBJECT (dec, "buffer without frame");
    gst_buffer_unref (buff);
    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    return;
  }

  frame->output_buffer = buff;

  /* We get the timestamp in ns already */
  frame->pts = droid_info.timestamp;

  /* we have a ref acquired by _get_oldest_frame()
   * but we don't drop it because _finish_frame() assumes 2 refs.
   * A ref that we obtained via _handle_frame() which we dropped already
   * so we need to compensate it and the 2nd ref which is already owned by
   * the base class GstVideoDecoder
   */
  dec->downstream_flow_ret = gst_droidvdec_finish_frame (decoder, frame);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  return;

acquire_and_release:
  /* we can not use our cb struct here so ask droidmedia to do
   * the work instead */
  droid_media_buffer_queue_acquire_and_release (dec->queue, NULL);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
}

static void
gst_droidvdec_data_available (void *data, DroidMediaCodecData * encoded)
{
  GstDroidVDec *dec = (GstDroidVDec *) data;
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
  GstBuffer *buff;
  GstFlowReturn flow_ret;
  GstVideoCodecFrame *frame;

  GST_DEBUG_OBJECT (dec, "data available");

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (dec->dirty) {
    flow_ret = dec->downstream_flow_ret;
    goto out;
  }

  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (dec, "not handling frame in error state: %s",
        gst_flow_get_name (dec->downstream_flow_ret));
    flow_ret = dec->downstream_flow_ret;
    goto out;
  }

  if (G_UNLIKELY (!dec->out_state)) {
    /* No need to pass anything for width and height as they will be overwritten anyway */
    if (!gst_droidvdec_configure_state (decoder, 0, 0)) {
      flow_ret = GST_FLOW_ERROR;
      goto out;
    }
  }

  buff = gst_video_decoder_allocate_output_buffer (decoder);

  gst_buffer_add_video_meta (buff, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_I420, dec->out_state->info.width,
      dec->out_state->info.height);

  if (!gst_droidvdec_convert_buffer (dec, buff, &encoded->data,
          &dec->out_state->info)) {
    gst_buffer_unref (buff);
    flow_ret = GST_FLOW_ERROR;
    goto out;
  }

  frame = gst_video_decoder_get_oldest_frame (decoder);

  if (G_UNLIKELY (!frame)) {
    /* TODO: what should we do here? */
    GST_WARNING_OBJECT (dec, "buffer without frame");
    gst_buffer_unref (buff);
    flow_ret = dec->downstream_flow_ret;
    goto out;
  }

  /* We get the timestamp in ns already */
  frame->pts = encoded->ts;
  frame->output_buffer = buff;

  /* we have a ref acquired by _get_oldest_frame()
   * but we don't drop it because _finish_frame() assumes 2 refs.
   * A ref that we obtained via _handle_frame() which we dropped already
   * so we need to compensate it and the 2nd ref which is already owned by
   * the base class GstVideoDecoder
   */
  flow_ret = gst_droidvdec_finish_frame (decoder, frame);

out:
  dec->downstream_flow_ret = flow_ret;
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  return;
}

static GstFlowReturn
gst_droidvdec_finish_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstFlowReturn flow_ret;

  GST_DEBUG_OBJECT (decoder, "finish frame");

  flow_ret = gst_video_decoder_finish_frame (decoder, frame);

  if (flow_ret == GST_FLOW_OK || flow_ret == GST_FLOW_FLUSHING) {
    goto out;
  } else if (flow_ret == GST_FLOW_EOS) {
    GST_INFO_OBJECT (decoder, "eos");
  } else if (flow_ret < GST_FLOW_OK) {
    GST_ELEMENT_ERROR (decoder, STREAM, FAILED,
        ("Internal data stream error."), ("stream stopped, reason %s",
            gst_flow_get_name (flow_ret)));
  }

out:
  return flow_ret;
}

static void
gst_droidvdec_signal_eos (void *data)
{
  GstDroidVDec *dec = (GstDroidVDec *) data;

  GST_DEBUG_OBJECT (dec, "codec signaled EOS");

  g_mutex_lock (&dec->state_lock);

  if (dec->state != GST_DROID_VDEC_STATE_WAITING_FOR_EOS) {
    GST_WARNING_OBJECT (dec, "codec signaled EOS but we are not expecting it");
  }

  if (dec->pool) {
    gst_buffer_pool_set_flushing (dec->pool, TRUE);
  }

  dec->state = GST_DROID_VDEC_STATE_EOS;

  g_cond_signal (&dec->state_cond);
  g_mutex_unlock (&dec->state_lock);
}

static void
gst_droidvdec_error (void *data, int err)
{
  GstDroidVDec *dec = (GstDroidVDec *) data;

  GST_DEBUG_OBJECT (dec, "codec error");

  g_mutex_lock (&dec->state_lock);

  if (dec->state == GST_DROID_VDEC_STATE_WAITING_FOR_EOS) {
    /* Gotta love Android. We will ignore errors if we are expecting EOS */
    g_cond_signal (&dec->state_cond);
    g_mutex_unlock (&dec->state_lock);
    goto out;
  }

  /* just in case */
  g_cond_signal (&dec->state_cond);

  g_mutex_unlock (&dec->state_lock);

  GST_VIDEO_DECODER_STREAM_LOCK (dec);
  dec->downstream_flow_ret = GST_FLOW_ERROR;
  GST_VIDEO_DECODER_STREAM_UNLOCK (dec);

  GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, NULL,
      ("error 0x%x from android codec", -err));

out:
  if (dec->pool) {
    gst_buffer_pool_set_flushing (dec->pool, TRUE);
  }
}

static int
gst_droidvdec_size_changed (void *data, int32_t width, int32_t height)
{
  GstDroidVDec *dec = (GstDroidVDec *) data;
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
  int err = 0;

  GST_INFO_OBJECT (dec, "size changed: w=%d, h=%d", width, height);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (dec->out_state) {
    gst_video_codec_state_unref (dec->out_state);
    dec->out_state = NULL;
  }

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return err;
}

static gboolean
gst_droidvdec_configure_state (GstVideoDecoder * decoder, guint width,
    guint height)
{
  GstDroidVDec *dec = GST_DROIDVDEC (decoder);
  DroidMediaCodecMetaData md;
  DroidMediaRect rect;
  DroidMediaColourFormatConstants constants;
  int format_index, format_count;

  const GstDroidVideoFormatMap formats[] = {
    {&constants.QOMX_COLOR_FormatYUV420PackedSemiPlanar32m,
          GST_VIDEO_FORMAT_NV12,
        gst_droidvdec_convert_yuv420_packed_semi_planar_to_i420, 1, 128, 32},
    {&constants.QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka,
        GST_VIDEO_FORMAT_NV12_64Z32, NULL, 0, 0, 0},
    {&constants.OMX_COLOR_FormatYUV420Planar, GST_VIDEO_FORMAT_I420,
        gst_droidvdec_convert_yuv420_planar_to_i420, 1, 4, 1},
    {&constants.OMX_COLOR_FormatYUV420PackedPlanar, GST_VIDEO_FORMAT_I420, NULL,
        1, 1, 1},
    {&constants.OMX_COLOR_FormatYUV420SemiPlanar, GST_VIDEO_FORMAT_NV12,
        gst_droidvdec_convert_yuv420_semi_planar_to_i420, 1, 1, 1},
    {&constants.OMX_COLOR_FormatL8, GST_VIDEO_FORMAT_GRAY8, NULL, 1, 1, 1},
    {&constants.OMX_COLOR_FormatYUV422SemiPlanar, GST_VIDEO_FORMAT_NV16, NULL,
        1, 1, 1},
    {&constants.OMX_COLOR_FormatYCbYCr, GST_VIDEO_FORMAT_YUY2, NULL, 1, 1, 1},
    {&constants.OMX_COLOR_FormatYCrYCb, GST_VIDEO_FORMAT_YVYU, NULL, 1, 1, 1},
    {&constants.OMX_COLOR_FormatCbYCrY, GST_VIDEO_FORMAT_UYVY, NULL, 1, 1, 1},
    {&constants.OMX_COLOR_Format32bitARGB8888,
          /* There is a mismatch in omxil specification 4.2.1 between
           * OMX_COLOR_Format32bitARGB8888 and its description
           * Follow the description */
          GST_VIDEO_FORMAT_ABGR,
          NULL,
        4, 4, 1},
    {&constants.OMX_COLOR_Format32bitBGRA8888,
          /* Same issue as OMX_COLOR_Format32bitARGB8888 */
          GST_VIDEO_FORMAT_ARGB,
          NULL,
        4, 4, 1},
    {&constants.OMX_COLOR_Format16bitRGB565, GST_VIDEO_FORMAT_RGB16, NULL, 2, 4,
        1},
    {&constants.OMX_COLOR_Format16bitBGR565, GST_VIDEO_FORMAT_BGR16, NULL, 2, 4,
        1}
  };

  memset (&md, 0x0, sizeof (md));
  memset (&rect, 0x0, sizeof (rect));

  droid_media_colour_format_constants_init (&constants);
  droid_media_codec_get_output_info (dec->codec, &md, &rect);

  GST_INFO_OBJECT (dec,
      "codec reported state: colour: %d, width: %d, height: %d, crop: %d,%d %d,%d",
      md.hal_format, md.width, md.height, rect.left, rect.top, rect.right,
      rect.bottom);

  dec->codec_reported_height = md.height;
  dec->codec_reported_width = md.width;
  dec->hal_format = md.hal_format;

  if (!dec->use_hardware_buffers && !dec->convert) {
    if (dec->codec_type->quirks & DONT_USE_DROID_CONVERT_VALUE) {
      GST_INFO_OBJECT (dec, "not using droid convert binary");
    } else {
      dec->convert = droid_media_convert_create ();
    }
  }

  format_count = sizeof (formats) / sizeof (formats[0]);
  for (format_index = 0; format_index < format_count; ++format_index) {
    if (*formats[format_index].hal_format == md.hal_format) {
      break;
    }
  }

  if (dec->use_hardware_buffers) {
    if (format_index < format_count) {
      dec->format = formats[format_index].gst_format;
      dec->bytes_per_pixel = formats[format_index].bytes_per_pixel;
      dec->h_align = formats[format_index].h_align;
      dec->v_align = formats[format_index].v_align;
    } else {
      GST_INFO_OBJECT (dec, "The HAL codec format 0x%x is unrecognized",
          md.hal_format);
      /* This should be GST_VIDEO_FORMAT_ENCODED but the videoconv? element
         can't do passthrough of that format. Since the format can't be
         identified the reported size of the buffer will be zero and it
         won't be possible to map it so reporting the wrong format should
         be harmless. */
      dec->format = GST_VIDEO_FORMAT_YV12;
      dec->bytes_per_pixel = 0;
      dec->h_align = 0;
      dec->v_align = 0;
    }
  } else {
    if (dec->convert) {
      dec->convert_to_i420 = gst_droidvdec_convert_native_to_i420;
    } else if (format_index < format_count) {
      dec->convert_to_i420 = formats[format_index].convert_to_i420;
    } else {
      dec->convert_to_i420 = NULL;
    }

    if (dec->convert_to_i420) {
      dec->format = GST_VIDEO_FORMAT_I420;

      width = rect.right - rect.left;
      height = rect.bottom - rect.top;
    } else {
      GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
          ("HAL codec format 0x%x is unsupported", md.hal_format));
      goto error;
    }
  }

  GST_INFO_OBJECT (dec, "configuring state: width=%d, height=%d", width,
      height);

  dec->out_state = gst_video_decoder_set_output_state (decoder,
      dec->format, width, height, dec->in_state);

  /* now the caps */
  g_assert (dec->out_state->caps == NULL);
  dec->out_state->caps = gst_video_info_to_caps (&dec->out_state->info);

  if (dec->use_hardware_buffers) {
    GstCapsFeatures *feature =
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_BUFFER,
        NULL);
    gst_caps_set_features (dec->out_state->caps, 0, feature);
  } else {
    memcpy (&dec->crop_rect, &rect, sizeof (rect));

    if (dec->convert) {
      droid_media_convert_set_crop_rect (dec->convert, rect, md.width,
          md.height);
      GST_INFO_OBJECT (dec, "using colour conversion for output buffers");
    }
  }

  if (!gst_video_decoder_negotiate (decoder)) {
    GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
        ("Failed to negotiate caps"));
    goto error;
  }

  GST_DEBUG_OBJECT (dec, "output caps %" GST_PTR_FORMAT, dec->out_state->caps);

  return TRUE;

error:
  if (dec->out_state) {
    gst_video_codec_state_unref (dec->out_state);
    dec->out_state = NULL;
  }

  if (dec->convert) {
    droid_media_convert_destroy (dec->convert);
    dec->convert = NULL;
  }

  return FALSE;
}

static gboolean
gst_droidvdec_stop (GstVideoDecoder * decoder)
{
  GstDroidVDec *dec = GST_DROIDVDEC (decoder);

  GST_DEBUG_OBJECT (dec, "stop");

  if (dec->codec) {
    droid_media_codec_stop (dec->codec);
    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;
    dec->queue = NULL;
  }

  if (dec->in_state) {
    gst_video_codec_state_unref (dec->in_state);
    dec->in_state = NULL;
  }

  if (dec->out_state) {
    gst_video_codec_state_unref (dec->out_state);
    dec->out_state = NULL;
  }

  gst_buffer_replace (&dec->codec_data, NULL);

  if (dec->codec_type) {
    gst_droid_codec_unref (dec->codec_type);
    dec->codec_type = NULL;
  }

  if (dec->convert) {
    droid_media_convert_destroy (dec->convert);
    dec->convert = NULL;
  }

  if (dec->pool) {
    gst_buffer_pool_set_active (dec->pool, FALSE);
  }

  return TRUE;
}

static void
gst_droidvdec_stop_loop (GstDroidVDec * dec)
{
  /*
   * we cannot use _stop() here because it tries to acquire stream lock
   * which frame_available also needs. This can deadlock because stop
   * won't complete before frame_available is done.
   */
  GST_DEBUG_OBJECT (dec, "stop loop");

  if (dec->pool) {
    gst_buffer_pool_set_flushing (dec->pool, TRUE);
  }

  gst_pad_stop_task (GST_VIDEO_DECODER_SRC_PAD (GST_VIDEO_DECODER (dec)));

  if (dec->pool) {
    gst_buffer_pool_set_flushing (dec->pool, FALSE);
  }

  dec->running = FALSE;
}

static void
gst_droidvdec_finalize (GObject * object)
{
  GstDroidVDec *dec = GST_DROIDVDEC (object);

  GST_DEBUG_OBJECT (dec, "finalize");

  gst_droidvdec_stop (GST_VIDEO_DECODER (dec));

  if (dec->pool) {
    gst_object_unref (dec->pool);
    dec->pool = NULL;
  }

  gst_object_unref (dec->allocator);
  dec->allocator = NULL;

  g_mutex_clear (&dec->state_lock);
  g_cond_clear (&dec->state_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_droidvdec_open (GstVideoDecoder * decoder)
{
  GstDroidVDec *dec = GST_DROIDVDEC (decoder);

  GST_DEBUG_OBJECT (dec, "open");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droidvdec_close (GstVideoDecoder * decoder)
{
  GstDroidVDec *dec = GST_DROIDVDEC (decoder);

  GST_DEBUG_OBJECT (dec, "close");

  /* nothing to do here */

  return TRUE;
}

static gboolean
gst_droidvdec_start (GstVideoDecoder * decoder)
{
  GstDroidVDec *dec = GST_DROIDVDEC (decoder);

  GST_DEBUG_OBJECT (dec, "start");

  dec->state = GST_DROID_VDEC_STATE_OK;
  dec->downstream_flow_ret = GST_FLOW_OK;
  dec->codec_type = NULL;
  dec->dirty = TRUE;
  dec->running = TRUE;
  dec->format = GST_VIDEO_FORMAT_UNKNOWN;
  dec->codec_reported_height = -1;
  dec->codec_reported_width = -1;

  return TRUE;
}

static gboolean
gst_droidvdec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstDroidVDec *dec = GST_DROIDVDEC (decoder);
  GstCaps *caps, *template_caps;
  GstCapsFeatures *features;
  guint i, count;

  /*
   * destroying the droidmedia codec here will cause stagefright to call abort.
   * That is why we create it after we are sure that everything is correct
   */

  GST_DEBUG_OBJECT (dec, "set format %" GST_PTR_FORMAT, state->caps);

  if (dec->codec) {
    GST_FIXME_OBJECT (dec, "What to do here?");
    GST_ERROR_OBJECT (dec, "codec already configured");
    return FALSE;
  }

  dec->codec_type =
      gst_droid_codec_new_from_caps (state->caps,
      GST_DROID_CODEC_DECODER_VIDEO);
  if (!dec->codec_type) {
    GST_ELEMENT_ERROR (dec, LIBRARY, FAILED, (NULL),
        ("Unknown codec type for caps %" GST_PTR_FORMAT, state->caps));
    return FALSE;
  }

  /* fill info */
  template_caps =
      gst_pad_get_pad_template_caps (GST_VIDEO_DECODER_SRC_PAD (decoder));
  caps =
      gst_pad_peer_query_caps (GST_VIDEO_DECODER_SRC_PAD (decoder),
      template_caps);
  gst_caps_unref (template_caps);
  template_caps = NULL;

  GST_DEBUG_OBJECT (dec, "peer caps %" GST_PTR_FORMAT, caps);

  dec->use_hardware_buffers = FALSE;

  count = gst_caps_get_size (caps);
  for (i = 0; i < count; ++i) {
    features = gst_caps_get_features (caps, i);
    if (gst_caps_features_contains
        (features, GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_BUFFER)) {
      dec->use_hardware_buffers = TRUE;
    }
  }

  gst_caps_unref (caps);

  if (G_UNLIKELY (count == 0)) {
    GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL), ("Failed to parse caps"));
    return FALSE;
  }

  dec->in_state = gst_video_codec_state_ref (state);

  if (dec->out_state) {
    gst_video_codec_state_unref (dec->out_state);
    dec->out_state = NULL;
  }

  gst_buffer_replace (&dec->codec_data, state->codec_data);

  /* handle_frame will create the codec */
  dec->dirty = TRUE;

  return TRUE;
}

static GstFlowReturn
gst_droidvdec_finish (GstVideoDecoder * decoder)
{
  GstDroidVDec *dec = GST_DROIDVDEC (decoder);

  GST_DEBUG_OBJECT (dec, "finish");

  g_mutex_lock (&dec->state_lock);

  /* since we release the stream lock, we can get called again */
  if (dec->state == GST_DROID_VDEC_STATE_WAITING_FOR_EOS) {
    GST_DEBUG_OBJECT (dec, "already finishing");
    g_mutex_unlock (&dec->state_lock);
    return GST_FLOW_NOT_SUPPORTED;
  }

  if (dec->codec && dec->state == GST_DROID_VDEC_STATE_OK) {
    GST_INFO_OBJECT (dec, "draining");
    dec->state = GST_DROID_VDEC_STATE_WAITING_FOR_EOS;
    droid_media_codec_drain (dec->codec);
  } else {
    goto out;
  }

  /* release the lock to allow _frame_available () to do its job */
  GST_LOG_OBJECT (dec, "releasing stream lock");
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  /* Now we wait for the codec to signal EOS */
  g_cond_wait (&dec->state_cond, &dec->state_lock);
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  GST_LOG_OBJECT (dec, "acquired stream lock");

  /* We drained the codec. Better to recreate it. */
  if (dec->codec) {
    droid_media_codec_stop (dec->codec);
    droid_media_codec_destroy (dec->codec);
    dec->codec = NULL;
    dec->queue = NULL;
  }

  dec->dirty = TRUE;

out:
  dec->state = GST_DROID_VDEC_STATE_OK;

  GST_DEBUG_OBJECT (dec, "finished");

  g_mutex_unlock (&dec->state_lock);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_droidvdec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstDroidVDec *dec = GST_DROIDVDEC (decoder);
  GstFlowReturn ret;
  DroidMediaCodecData data;
  DroidMediaBufferCallbacks cb;

  GST_DEBUG_OBJECT (dec, "handle frame");

  if (G_UNLIKELY (!dec->running)) {
    GST_DEBUG_OBJECT (dec, "codec is not running");
    ret = GST_FLOW_FLUSHING;
    goto error;
  }

  if (!GST_CLOCK_TIME_IS_VALID (frame->dts)
      && !GST_CLOCK_TIME_IS_VALID (frame->pts)) {
    GST_WARNING_OBJECT (dec,
        "dropping received frame with invalid timestamps.");
    ret = GST_FLOW_OK;
    goto error;
  }

  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (dec, "not handling frame in error state: %s",
        gst_flow_get_name (dec->downstream_flow_ret));
    ret = dec->downstream_flow_ret;
    goto error;
  }

  g_mutex_lock (&dec->state_lock);
  if (dec->state == GST_DROID_VDEC_STATE_EOS) {
    GST_WARNING_OBJECT (dec, "got frame in eos state");
    g_mutex_unlock (&dec->state_lock);
    ret = GST_FLOW_EOS;
    goto error;
  }
  g_mutex_unlock (&dec->state_lock);

  /* We must create the codec before we process any data. _create_codec will call
   * construct_decoder_codec_data which will store the nal prefix length for H264.
   * This is a bad situation. TODO: fix it
   */
  if (G_UNLIKELY (dec->dirty)) {
    if (!GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame)) {
      ret = GST_FLOW_OK;
      gst_video_decoder_drop_frame (decoder, frame);
      goto out;
    }

    if (dec->codec) {
      gst_droidvdec_finish (decoder);
    }

    if (!gst_droidvdec_create_codec (dec, frame->input_buffer)) {
      ret = GST_FLOW_ERROR;
      goto error;
    }

    dec->dirty = FALSE;
  }

  if (!gst_droid_codec_prepare_decoder_frame (dec->codec_type, frame,
          &data.data, &cb)) {
    ret = GST_FLOW_ERROR;
    GST_ELEMENT_ERROR (dec, STREAM, FORMAT, (NULL),
        ("Failed to prepare data for decoding"));
    goto error;
  }

  /*
   * try to use dts if pts is not valid.
   * on one of the test streams we get the first PTS set to GST_CLOCK_TIME_NONE
   * which breaks timestamping.
   */
  data.ts =
      GST_CLOCK_TIME_IS_VALID (frame->pts) ? GST_TIME_AS_USECONDS (frame->
      pts) : GST_TIME_AS_USECONDS (frame->dts);
  data.sync = GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame) ? true : false;

  /* This can deadlock if droidmedia/stagefright input buffer queue is full thus we
   * cannot write the input buffer. We end up waiting for the write operation
   * which does not happen because stagefright needs us to provide
   * output buffers to be filled (which can not happen because _loop() tries
   * to call get_oldest_frame() which acquires the stream lock the base class
   * is holding before calling us
   */
  GST_LOG_OBJECT (dec, "releasing stream lock");
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  droid_media_codec_queue (dec->codec, &data, &cb);
  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  GST_LOG_OBJECT (dec, "acquired stream lock");

  /* from now on decoder owns a frame reference */

  if (dec->downstream_flow_ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (dec, "not handling frame in error state: %s",
        gst_flow_get_name (dec->downstream_flow_ret));
    ret = dec->downstream_flow_ret;
    goto unref;
  }

  g_mutex_lock (&dec->state_lock);
  if (dec->state == GST_DROID_VDEC_STATE_EOS) {
    GST_WARNING_OBJECT (dec, "got frame in eos state");
    g_mutex_unlock (&dec->state_lock);
    ret = GST_FLOW_EOS;
    goto unref;
  }

  g_mutex_unlock (&dec->state_lock);

  ret = GST_FLOW_OK;

  goto unref;

out:
  return ret;

unref:
  gst_video_codec_frame_unref (frame);
  goto out;

error:
  /* don't leak the frame */
  gst_video_decoder_release_frame (decoder, frame);

  return ret;
}

static gboolean
gst_droidvdec_flush (GstVideoDecoder * decoder)
{
  GstDroidVDec *dec = GST_DROIDVDEC (decoder);

  GST_DEBUG_OBJECT (dec, "flush");

  /* We cannot flush the frames being decoded from the decoder. There is simply no way
   * to do that. The best we can do is to clear the queue of frames to be encoded.
   * The problem now is if we get flushed we will still decode the previous queued frames
   * and push them later on when they get decoded.
   * This will lead to frames being repeated if the flush happens in the beginning
   * or inaccurate seeking.
   * We will just mark the decoder as "dirty" so the next handle_frame can recreate it
   */

  dec->dirty = TRUE;

  dec->downstream_flow_ret = GST_FLOW_OK;
  g_mutex_lock (&dec->state_lock);
  dec->state = GST_DROID_VDEC_STATE_OK;
  g_mutex_unlock (&dec->state_lock);

  return TRUE;
}

static GstStateChangeReturn
gst_droidvdec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstDroidVDec *dec;

  dec = GST_DROIDVDEC (element);

  GST_DEBUG_OBJECT (dec, "change state from %s to %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY) {
    GstVideoDecoder *decoder = GST_VIDEO_DECODER (dec);
    GstFlowReturn finish_res = GST_FLOW_OK;

    GST_VIDEO_DECODER_STREAM_LOCK (decoder);
    /*
     * _loop() can be waiting in droid_media_codec_loop() thus we must make
     * sure the stagefright decoder is not doing anything otherwise
     * gst_droidvdec_stop_loop() will deadlock
     */
    if (dec->codec) {
      finish_res = gst_droidvdec_finish (decoder);
    }

    if (finish_res != GST_FLOW_NOT_SUPPORTED) {
      gst_droidvdec_stop_loop (dec);
    }

    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static void
gst_droidvdec_init (GstDroidVDec * dec)
{
  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (dec), TRUE);

  dec->codec = NULL;
  dec->queue = NULL;
  dec->codec_type = NULL;
  dec->downstream_flow_ret = GST_FLOW_OK;
  dec->state = GST_DROID_VDEC_STATE_OK;
  dec->codec_data = NULL;
  dec->codec_reported_height = -1;
  dec->codec_reported_width = -1;

  g_mutex_init (&dec->state_lock);
  g_cond_init (&dec->state_cond);

  dec->allocator = gst_droid_media_buffer_allocator_new ();
  dec->in_state = NULL;
  dec->out_state = NULL;
  dec->convert = NULL;
  dec->pool = NULL;
}

static void
gst_droidvdec_class_init (GstDroidVDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideoDecoderClass *gstvideodecoder_class;
  GstCaps *caps;
  GstPadTemplate *tpl;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstvideodecoder_class = (GstVideoDecoderClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Video decoder", "Decoder/Video/Device",
      "Android HAL decoder", "Mohammed Sameer <msameer@foolab.org>");

  caps = gst_droid_codec_get_all_caps (GST_DROID_CODEC_DECODER_VIDEO);
  tpl = gst_pad_template_new (GST_VIDEO_DECODER_SINK_NAME,
      GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (gstelement_class, tpl);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droidvdec_src_template_factory));

  gobject_class->finalize = gst_droidvdec_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_droidvdec_change_state);
  gstvideodecoder_class->open = GST_DEBUG_FUNCPTR (gst_droidvdec_open);
  gstvideodecoder_class->close = GST_DEBUG_FUNCPTR (gst_droidvdec_close);
  gstvideodecoder_class->start = GST_DEBUG_FUNCPTR (gst_droidvdec_start);
  gstvideodecoder_class->stop = GST_DEBUG_FUNCPTR (gst_droidvdec_stop);
  gstvideodecoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_droidvdec_set_format);
  gstvideodecoder_class->finish = GST_DEBUG_FUNCPTR (gst_droidvdec_finish);
  gstvideodecoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_droidvdec_handle_frame);
  gstvideodecoder_class->flush = GST_DEBUG_FUNCPTR (gst_droidvdec_flush);
}
