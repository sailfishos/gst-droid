/*
 * gst-droid
 *
 * Copyright (C) 2026 Jolla Ltd.
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

/*
 * droidscreencapsrc records the display with the hardware H.264 encoder. The
 * compositor (qt5-qpa-hwcomposer-plugin, QPA_HWC_SCREENCAP=1) renders each new
 * frame into the encoder's input Surface, so an idle screen sends nothing and
 * the PTS keep the gaps. One access unit per buffer, SPS/PPS on every IDR; an
 * upstream GstForceKeyUnit gives an IDR of the current screen.
 *
 *   gst-launch-1.0 -e droidscreencapsrc width=1080 height=2520 ! \
 *       h264parse ! video/x-h264,stream-format=avc,alignment=au ! \
 *       qtmux ! filesink location=/tmp/screen.mp4
 *   gst-launch-1.0 droidscreencapsrc width=1080 height=2520 ! \
 *       tcpserversink host=0.0.0.0 port=5000 sync=false
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstdroidscreencapsrc.h"
#include <errno.h>
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_droid_screencapsrc_debug);
#define GST_CAT_DEFAULT gst_droid_screencapsrc_debug

#define DEFAULT_WIDTH           0       /* required */
#define DEFAULT_HEIGHT          0       /* required */
#define DEFAULT_TARGET_BITRATE  8000000
#define DEFAULT_FPS             60
#define DEFAULT_PROFILE         0       /* the encoder's */
#define DEFAULT_LEVEL           0       /* the lowest that fits */
#define DEFAULT_TARGET          DROID_MEDIA_SCREEN_CAPTURE_TARGET_DISPLAY

/* On overflow, queued access units are dropped up to the next IDR. */
#define OUTPUT_QUEUE_MAX_FRAMES 30

/* Bound for draining the encoder at EOS, after droidmedia's detach grace. */
#define FINISH_TIMEOUT_MS       3000

enum
{
  PROP_0,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_TARGET_BITRATE,
  PROP_FPS,
  PROP_PROFILE,
  PROP_LEVEL,
  PROP_TARGET,
  PROP_CONTENT_RECT,
};

#define GST_TYPE_DROIDSCREENCAPSRC_PROFILE \
  (gst_droidscreencapsrc_profile_get_type ())

static GType
gst_droidscreencapsrc_profile_get_type (void)
{
  static GType type = 0;
  static const GEnumValue profiles[] = {
    {DEFAULT_PROFILE, "Encoder default", "default"},
    {DROID_MEDIA_CODEC_H264_PROFILE_CONSTRAINED_BASELINE,
        "Constrained Baseline", "constrained-baseline"},
    {DROID_MEDIA_CODEC_H264_PROFILE_BASELINE, "Baseline", "baseline"},
    {DROID_MEDIA_CODEC_H264_PROFILE_MAIN, "Main", "main"},
    {DROID_MEDIA_CODEC_H264_PROFILE_HIGH, "High", "high"},
    {0, NULL, NULL}
  };

  if (!type)
    type = g_enum_register_static ("GstDroidScreenCapSrcProfile", profiles);
  return type;
}

#define GST_TYPE_DROIDSCREENCAPSRC_TARGET \
  (gst_droidscreencapsrc_target_get_type ())

static GType
gst_droidscreencapsrc_target_get_type (void)
{
  static GType type = 0;
  static const GEnumValue targets[] = {
    {DROID_MEDIA_SCREEN_CAPTURE_TARGET_DISPLAY,
        "The compositor mirrors the display", "display"},
    {DROID_MEDIA_SCREEN_CAPTURE_TARGET_VIRTUAL,
        "A renderer of its own; the compositor ignores the session", "virtual"},
    {0, NULL, NULL}
  };

  if (!type)
    type = g_enum_register_static ("GstDroidScreenCapSrcTarget", targets);
  return type;
}

/* level_idc (level times 10) to the droidmedia level, 0 if unknown. */
static gint
gst_droidscreencapsrc_level (gint level_idc)
{
  static const struct
  {
    gint level_idc;
    gint level;
  } levels[] = {
    {10, DROID_MEDIA_CODEC_H264_LEVEL_1_0},
    {11, DROID_MEDIA_CODEC_H264_LEVEL_1_1},
    {12, DROID_MEDIA_CODEC_H264_LEVEL_1_2},
    {13, DROID_MEDIA_CODEC_H264_LEVEL_1_3},
    {20, DROID_MEDIA_CODEC_H264_LEVEL_2_0},
    {21, DROID_MEDIA_CODEC_H264_LEVEL_2_1},
    {22, DROID_MEDIA_CODEC_H264_LEVEL_2_2},
    {30, DROID_MEDIA_CODEC_H264_LEVEL_3_0},
    {31, DROID_MEDIA_CODEC_H264_LEVEL_3_1},
    {32, DROID_MEDIA_CODEC_H264_LEVEL_3_2},
    {40, DROID_MEDIA_CODEC_H264_LEVEL_4_0},
    {41, DROID_MEDIA_CODEC_H264_LEVEL_4_1},
    {42, DROID_MEDIA_CODEC_H264_LEVEL_4_2},
    {50, DROID_MEDIA_CODEC_H264_LEVEL_5_0},
    {51, DROID_MEDIA_CODEC_H264_LEVEL_5_1},
    {52, DROID_MEDIA_CODEC_H264_LEVEL_5_2},
  };
  guint i;

  for (i = 0; i < G_N_ELEMENTS (levels); i++) {
    if (levels[i].level_idc == level_idc)
      return levels[i].level;
  }
  return 0;
}

/* framerate in the caps is nominal: idle periods carry no access units. */
static GstStaticPadTemplate gst_droidscreencapsrc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, stream-format = (string) byte-stream, "
        "alignment = (string) au"));

#define gst_droidscreencapsrc_parent_class parent_class
G_DEFINE_TYPE (GstDroidScreenCapSrc, gst_droidscreencapsrc, GST_TYPE_PUSH_SRC);

/* Keeps the first error; create() posts it. Requires output_lock. */
static void
gst_droidscreencapsrc_set_error_locked (GstDroidScreenCapSrc * src, gint err)
{
  if (src->error == 0) {
    src->error = err != 0 ? err : -1;
    GST_ERROR_OBJECT (src, "run terminated by error %d", src->error);
  }
  g_cond_broadcast (&src->output_cond);
}

/* Encoder callbacks run on the droidmedia output thread and must not block. */
static void
gst_droidscreencapsrc_data_available (void *data, DroidMediaCodecData * encoded)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (data);
  GstClockTime pts;
  GstBuffer *buf;
  guint queued;

  GST_LOG_OBJECT (src, "data_available: %" G_GSIZE_FORMAT " bytes, ts %"
      G_GINT64_FORMAT " sync=%d codec_config=%d",
      (gsize) encoded->data.size, encoded->ts, encoded->sync,
      encoded->codec_config);

  /* Every sync frame starts with the SPS/PPS. On their own, h264parse turns
   * them into an access unit without PTS, which qtmux rejects. */
  if (encoded->codec_config)
    return;

  g_mutex_lock (&src->output_lock);

  if (src->error != 0) {
    g_mutex_unlock (&src->output_lock);
    return;
  }

  if (!src->have_first_pts) {
    src->have_first_pts = TRUE;
    src->first_ts = encoded->ts;
    GST_INFO_OBJECT (src, "timestamp origin: codec ts %" G_GINT64_FORMAT,
        src->first_ts);
  } else if (encoded->ts < src->last_ts) {
    GST_ERROR_OBJECT (src, "regressing codec ts %" G_GINT64_FORMAT
        " after %" G_GINT64_FORMAT, encoded->ts, src->last_ts);
    gst_droidscreencapsrc_set_error_locked (src, -EINVAL);
    g_mutex_unlock (&src->output_lock);
    return;
  } else if (encoded->ts == src->last_ts) {
    /* The encoder may emit its first IDR twice. Muxers reject a
     * zero-duration sample, so keep the first one. */
    GST_WARNING_OBJECT (src, "discarding access unit repeating codec ts %"
        G_GINT64_FORMAT " (%" G_GSIZE_FORMAT " bytes, sync=%d)",
        encoded->ts, (gsize) encoded->data.size, encoded->sync);
    g_mutex_unlock (&src->output_lock);
    return;
  }

  src->last_ts = encoded->ts;
  pts = encoded->ts - src->first_ts;

  if (src->waiting_for_keyframe) {
    if (!encoded->sync) {
      src->dropped_frames++;
      g_mutex_unlock (&src->output_lock);
      return;
    }
    GST_WARNING_OBJECT (src, "resuming at keyframe PTS %" GST_TIME_FORMAT
        ", %u frames dropped in this run", GST_TIME_ARGS (pts),
        src->dropped_frames);
    src->waiting_for_keyframe = FALSE;
  }

  queued = g_queue_get_length (src->output_queue);
  if (queued >= OUTPUT_QUEUE_MAX_FRAMES) {
    GST_WARNING_OBJECT (src, "output queue full: dropping %u frames and "
        "discarding until the next keyframe", queued + 1);
    g_queue_clear_full (src->output_queue, (GDestroyNotify) gst_buffer_unref);
    src->dropped_frames += queued + 1;
    src->waiting_for_keyframe = TRUE;
    g_mutex_unlock (&src->output_lock);

    /* Resume at the next frame, not at the encoder's next periodic IDR. The
     * output thread is joined before capture is destroyed. */
    droid_media_screen_capture_request_sync_frame (src->capture);
    return;
  }

  buf = gst_buffer_new_allocate (NULL, encoded->data.size, NULL);
  gst_buffer_fill (buf, 0, encoded->data.data, encoded->data.size);
  if (!encoded->sync)
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
  /* No duration: it needs the next access unit, which an idle screen may not
   * send. */
  GST_BUFFER_PTS (buf) = pts;

  g_queue_push_tail (src->output_queue, buf);
  g_cond_signal (&src->output_cond);
  g_mutex_unlock (&src->output_lock);
}

static void
gst_droidscreencapsrc_error (void *data, int err)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (data);

  GST_ERROR_OBJECT (src, "encoder error %d (0x%x)", err, (unsigned) -err);

  g_mutex_lock (&src->output_lock);
  gst_droidscreencapsrc_set_error_locked (src, err);
  g_mutex_unlock (&src->output_lock);
}

static gboolean
gst_droidscreencapsrc_is_force_key_unit (GstEvent * event)
{
  return GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_UPSTREAM &&
      gst_event_has_name (event, "GstForceKeyUnit");
}

/* stop() takes capture_lock to clear capture, but only after the encoder has
 * stopped, so this is safe from any thread. */
static void
gst_droidscreencapsrc_force_key_unit (GstDroidScreenCapSrc * src)
{
  g_mutex_lock (&src->capture_lock);
  if (src->capture && droid_media_screen_capture_request_sync_frame
      (src->capture))
    GST_DEBUG_OBJECT (src, "requested a key unit");
  else
    GST_DEBUG_OBJECT (src, "not recording; key unit request ignored");
  g_mutex_unlock (&src->capture_lock);
}

/* GstBaseSrc ends the stream on EOS without the create() that drains the
 * encoder, and drops a GstForceKeyUnit sent to the element. Under STATE_LOCK,
 * like start() and stop(). */
static gboolean
gst_droidscreencapsrc_send_event (GstElement * element, GstEvent * event)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (element);

  if (gst_droidscreencapsrc_is_force_key_unit (event)) {
    gst_droidscreencapsrc_force_key_unit (src);
    gst_event_unref (event);
    return TRUE;
  }

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    gboolean handled;

    g_mutex_lock (&src->output_lock);
    handled = src->capture != NULL && !src->flushing;
    if (handled) {
      GST_INFO_OBJECT (src, "EOS requested; finishing encoder gracefully");
      src->eos_requested = TRUE;
      g_cond_broadcast (&src->output_cond);
    }
    g_mutex_unlock (&src->output_lock);

    if (handled) {
      gst_event_unref (event);
      return TRUE;
    }
  }

  return GST_ELEMENT_CLASS (parent_class)->send_event (element, event);
}

/* The upstream GstForceKeyUnit event, from downstream elements. */
static gboolean
gst_droidscreencapsrc_event (GstBaseSrc * bsrc, GstEvent * event)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (bsrc);

  if (gst_droidscreencapsrc_is_force_key_unit (event)) {
    gst_droidscreencapsrc_force_key_unit (src);
    return TRUE;
  }

  return GST_BASE_SRC_CLASS (parent_class)->event (bsrc, event);
}

static GstCaps *
gst_droidscreencapsrc_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (bsrc);
  GstCaps *caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));

  if (src->width > 0 && src->height > 0) {
    caps = gst_caps_make_writable (caps);
    gst_caps_set_simple (caps,
        "width", G_TYPE_INT, src->width,
        "height", G_TYPE_INT, src->height,
        "framerate", GST_TYPE_FRACTION, src->fps, 1, NULL);
  }

  if (filter) {
    GstCaps *tmp = gst_caps_intersect_full (filter, caps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = tmp;
  }

  GST_DEBUG_OBJECT (src, "caps %" GST_PTR_FORMAT, caps);
  return caps;
}

/* Only after a start() that set capture, and never during create(). */
static gboolean
gst_droidscreencapsrc_stop (GstBaseSrc * bsrc)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (bsrc);
  DroidMediaScreenCapture *capture = src->capture;

  GST_DEBUG_OBJECT (src, "stop");

  /* Not under output_lock, which a callback may be waiting for. capture_lock
   * is only held to clear capture, so a key unit request never waits. */
  droid_media_screen_capture_stop (capture);

  g_mutex_lock (&src->capture_lock);
  src->capture = NULL;
  g_mutex_unlock (&src->capture_lock);
  droid_media_screen_capture_destroy (capture);

  /* The output thread has stopped: reset the run state for the next start(). */
  g_mutex_lock (&src->output_lock);
  GST_INFO_OBJECT (src, "run summary: %" G_GUINT64_FORMAT " frames pushed, "
      "%u dropped", src->frames_out, src->dropped_frames);
  g_queue_clear_full (src->output_queue, (GDestroyNotify) gst_buffer_unref);
  src->have_first_pts = FALSE;
  src->waiting_for_keyframe = FALSE;
  src->dropped_frames = 0;
  src->eos_requested = FALSE;
  src->eos = FALSE;
  src->error = 0;
  src->frames_out = 0;
  g_mutex_unlock (&src->output_lock);

  return TRUE;
}

static gboolean
gst_droidscreencapsrc_start (GstBaseSrc * bsrc)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (bsrc);
  DroidMediaScreenCaptureCallbacks cb;
  DroidMediaCodecEncoderMetaData meta;
  DroidMediaScreenCapture *capture;
  gint level = 0;

  GST_DEBUG_OBJECT (src, "start");

  if (src->width <= 0 || src->height <= 0) {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS,
        ("The width and height properties must be set"), (NULL));
    return FALSE;
  }

  if (src->level != 0) {
    level = gst_droidscreencapsrc_level (src->level);
    if (level == 0 || src->profile == DEFAULT_PROFILE) {
      GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS,
          ("Invalid H.264 level"),
          ("level=%d needs a profile and one of 10-13, 20-22, 30-32, 40-42, "
              "50-52", src->level));
      return FALSE;
    }
  }

  memset (&meta, 0, sizeof (meta));
  meta.parent.width = src->width;
  meta.parent.height = src->height;
  meta.parent.fps = src->fps;
  meta.bitrate = src->target_bitrate;
  meta.codec_specific.h264.profile = src->profile;
  meta.codec_specific.h264.level = level;

  memset (&cb, 0, sizeof (cb));
  cb.data_available = gst_droidscreencapsrc_data_available;
  cb.error = gst_droidscreencapsrc_error;

  capture = droid_media_screen_capture_new (&meta, &cb, src);
  if (capture == NULL) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Failed to create the hardware AVC Surface encoder"),
        ("%dx%d @%d fps, %d bps", src->width, src->height, src->fps,
            src->target_bitrate));
    return FALSE;
  }

  if (!droid_media_screen_capture_set_target (capture, src->target,
          src->content_rect)) {
    droid_media_screen_capture_destroy (capture);
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS,
        ("Invalid content rectangle"),
        ("content-rect=<%d, %d, %d, %d> is not inside %dx%d",
            src->content_rect.left, src->content_rect.top,
            src->content_rect.right - src->content_rect.left,
            src->content_rect.bottom - src->content_rect.top, src->width,
            src->height));
    return FALSE;
  }

  /* The callbacks may fire, and use capture, as soon as the encoder starts. */
  g_mutex_lock (&src->capture_lock);
  src->capture = capture;
  g_mutex_unlock (&src->capture_lock);

  if (!droid_media_screen_capture_start (capture)) {
    gst_droidscreencapsrc_stop (bsrc);
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("Failed to start screen capture"),
        ("could not start the encoder or register it with "
            "sailfish.screencap; see logcat"));
    return FALSE;
  }

  GST_INFO_OBJECT (src, "started: %dx%d @%d fps, %d bps, profile 0x%x level "
      "%d, target %d, content %d,%d-%d,%d, queue bound %d frames",
      src->width, src->height, src->fps, src->target_bitrate, src->profile,
      src->level, src->target, src->content_rect.left, src->content_rect.top,
      src->content_rect.right, src->content_rect.bottom,
      OUTPUT_QUEUE_MAX_FRAMES);
  return TRUE;
}

static gboolean
gst_droidscreencapsrc_unlock (GstBaseSrc * bsrc)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (bsrc);

  GST_DEBUG_OBJECT (src, "unlock");

  g_mutex_lock (&src->output_lock);
  src->flushing = TRUE;
  g_cond_broadcast (&src->output_cond);
  g_mutex_unlock (&src->output_lock);

  return TRUE;
}

static gboolean
gst_droidscreencapsrc_unlock_stop (GstBaseSrc * bsrc)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (bsrc);

  GST_DEBUG_OBJECT (src, "unlock_stop");

  g_mutex_lock (&src->output_lock);
  src->flushing = FALSE;
  g_mutex_unlock (&src->output_lock);

  return TRUE;
}

static GstFlowReturn
gst_droidscreencapsrc_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (psrc);
  GstBuffer *buf;

  g_mutex_lock (&src->output_lock);

  for (;;) {
    if (src->flushing) {
      g_mutex_unlock (&src->output_lock);
      return GST_FLOW_FLUSHING;
    }

    buf = g_queue_pop_head (src->output_queue);
    if (buf != NULL)
      break;

    if (src->error != 0) {
      gint err = src->error;

      g_mutex_unlock (&src->output_lock);
      GST_ELEMENT_ERROR (src, LIBRARY, FAILED,
          ("Screen capture encoder failed"),
          ("encoder error %d (0x%x)", err, (unsigned) -err));
      return GST_FLOW_ERROR;
    }

    if (src->eos) {
      g_mutex_unlock (&src->output_lock);
      GST_INFO_OBJECT (src, "EOS after %" G_GUINT64_FORMAT " frames",
          src->frames_out);
      return GST_FLOW_EOS;
    }

    if (src->eos_requested) {
      g_mutex_unlock (&src->output_lock);
      GST_INFO_OBJECT (src, "finishing encoder (timeout %d ms)",
          FINISH_TIMEOUT_MS);
      if (!droid_media_screen_capture_finish (src->capture, FINISH_TIMEOUT_MS))
        GST_WARNING_OBJECT (src, "encoder did not reach codec EOS; "
            "finalizing with the data received so far");
      g_mutex_lock (&src->output_lock);
      /* The output thread has stopped; nothing more can arrive. */
      src->eos = TRUE;
      continue;
    }

    g_cond_wait (&src->output_cond, &src->output_lock);
  }

  src->frames_out++;
  g_mutex_unlock (&src->output_lock);

  GST_DEBUG_OBJECT (src, "pushing %" GST_PTR_FORMAT, buf);
  *outbuf = buf;
  return GST_FLOW_OK;
}

static void
gst_droidscreencapsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (object);

  switch (prop_id) {
    case PROP_WIDTH:
      src->width = g_value_get_int (value);
      break;
    case PROP_HEIGHT:
      src->height = g_value_get_int (value);
      break;
    case PROP_TARGET_BITRATE:
      src->target_bitrate = g_value_get_int (value);
      break;
    case PROP_FPS:
      src->fps = g_value_get_int (value);
      break;
    case PROP_PROFILE:
      src->profile = g_value_get_enum (value);
      break;
    case PROP_LEVEL:
      src->level = g_value_get_int (value);
      break;
    case PROP_TARGET:
      src->target = g_value_get_enum (value);
      break;
    case PROP_CONTENT_RECT:{
      gint x, y, w, h;

      if (gst_value_array_get_size (value) == 4) {
        x = g_value_get_int (gst_value_array_get_value (value, 0));
        y = g_value_get_int (gst_value_array_get_value (value, 1));
        w = g_value_get_int (gst_value_array_get_value (value, 2));
        h = g_value_get_int (gst_value_array_get_value (value, 3));
        if (w <= G_MAXINT - x && h <= G_MAXINT - y) {
          src->content_rect.left = x;
          src->content_rect.top = y;
          src->content_rect.right = x + w;
          src->content_rect.bottom = y + h;
          break;
        }
      }
      GST_WARNING_OBJECT (src, "ignoring content-rect: expected "
          "<x, y, width, height> with x + width and y + height in gint range");
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_droidscreencapsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (object);

  switch (prop_id) {
    case PROP_WIDTH:
      g_value_set_int (value, src->width);
      break;
    case PROP_HEIGHT:
      g_value_set_int (value, src->height);
      break;
    case PROP_TARGET_BITRATE:
      g_value_set_int (value, src->target_bitrate);
      break;
    case PROP_FPS:
      g_value_set_int (value, src->fps);
      break;
    case PROP_PROFILE:
      g_value_set_enum (value, src->profile);
      break;
    case PROP_LEVEL:
      g_value_set_int (value, src->level);
      break;
    case PROP_TARGET:
      g_value_set_enum (value, src->target);
      break;
    case PROP_CONTENT_RECT:{
      const gint rect[4] = { src->content_rect.left, src->content_rect.top,
        src->content_rect.right - src->content_rect.left,
        src->content_rect.bottom - src->content_rect.top
      };
      GValue v = G_VALUE_INIT;
      guint i;

      g_value_init (&v, G_TYPE_INT);
      for (i = 0; i < G_N_ELEMENTS (rect); i++) {
        g_value_set_int (&v, rect[i]);
        gst_value_array_append_value (value, &v);
      }
      g_value_unset (&v);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_droidscreencapsrc_finalize (GObject * object)
{
  GstDroidScreenCapSrc *src = GST_DROIDSCREENCAPSRC (object);

  g_queue_free (src->output_queue);
  g_mutex_clear (&src->output_lock);
  g_cond_clear (&src->output_cond);
  g_mutex_clear (&src->capture_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_droidscreencapsrc_init (GstDroidScreenCapSrc * src)
{
  src->width = DEFAULT_WIDTH;
  src->height = DEFAULT_HEIGHT;
  src->target_bitrate = DEFAULT_TARGET_BITRATE;
  src->fps = DEFAULT_FPS;
  src->profile = DEFAULT_PROFILE;
  src->level = DEFAULT_LEVEL;
  src->target = DEFAULT_TARGET;

  g_mutex_init (&src->capture_lock);
  g_mutex_init (&src->output_lock);
  g_cond_init (&src->output_cond);
  src->output_queue = g_queue_new ();

  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);
}

static void
gst_droidscreencapsrc_class_init (GstDroidScreenCapSrcClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseSrcClass *gstbasesrc_class = (GstBaseSrcClass *) klass;
  GstPushSrcClass *gstpushsrc_class = (GstPushSrcClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_droidscreencapsrc_src_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "Sailfish screen capture source",
      "Source/Video",
      "Records the display through the hardware AVC encoder input Surface",
      "Jolla Ltd.");

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_droidscreencapsrc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_droidscreencapsrc_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_droidscreencapsrc_finalize);

  gstelement_class->send_event =
      GST_DEBUG_FUNCPTR (gst_droidscreencapsrc_send_event);

  gstbasesrc_class->event = GST_DEBUG_FUNCPTR (gst_droidscreencapsrc_event);
  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_droidscreencapsrc_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_droidscreencapsrc_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_droidscreencapsrc_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_droidscreencapsrc_unlock_stop);
  gstbasesrc_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_droidscreencapsrc_get_caps);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_droidscreencapsrc_create);

  g_object_class_install_property (gobject_class, PROP_WIDTH,
      g_param_spec_int ("width", "Width",
          "Recording width in pixels (required, even); the display is fitted "
          "into the frame",
          0, G_MAXINT, DEFAULT_WIDTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_HEIGHT,
      g_param_spec_int ("height", "Height",
          "Recording height in pixels (required, even)",
          0, G_MAXINT, DEFAULT_HEIGHT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_TARGET_BITRATE,
      g_param_spec_int ("target-bitrate", "Target Bitrate",
          "Target bitrate in bits per second", 1, G_MAXINT,
          DEFAULT_TARGET_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_FPS,
      g_param_spec_int ("fps", "FPS",
          "Nominal frame rate for the encoder and the caps; use the display "
          "refresh rate",
          1, 120, DEFAULT_FPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_PROFILE,
      g_param_spec_enum ("profile", "Profile",
          "H.264 profile; default leaves profile and level to the encoder",
          GST_TYPE_DROIDSCREENCAPSRC_PROFILE, DEFAULT_PROFILE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_LEVEL,
      g_param_spec_int ("level", "Level",
          "H.264 level times 10 (31 is 3.1), 0 for the lowest that fits; needs "
          "a profile",
          0, 52, DEFAULT_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_TARGET,
      g_param_spec_enum ("target", "Target",
          "Who renders into the recording", GST_TYPE_DROIDSCREENCAPSRC_TARGET,
          DEFAULT_TARGET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_CONTENT_RECT,
      gst_param_spec_array ("content-rect", "Content rectangle",
          "Where the picture goes in the frame ('<x, y, width, height>', all 0 "
          "for the whole frame)",
          g_param_spec_int ("rect-value", "Rectangle Value",
              "One of x, y, width or height", 0, G_MAXINT, 0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
}
