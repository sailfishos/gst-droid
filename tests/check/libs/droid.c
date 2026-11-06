/*
 * gst-droid
 *
 * Copyright (C) 2026 Herman van Hazendonk <github.com@herrie.org>
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
 * Tests for libgstdroid's codec_data and frame parsers, its media buffer
 * allocator and its buffer pool, run without Android: the droidmedia calls they make are
 * defined here, and take the place of the libhybris glue in libgstdroid.
 *
 * The parsers are given every short size, so that a read past the end of
 * the data is caught by AddressSanitizer or valgrind: each buffer is
 * allocated with exactly its own size.
 */

#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <string.h>

#include <droidmediaconstants.h>

#include "gst/droid/gstdroidbufferpool.h"
#include "gst/droid/gstdroidcodec.h"
#include "gst/droid/gstdroidmediabuffer.h"

/* --- droidmedia, as far as these tests use it --- */

#define FAKE_HAL_RGBA 1
#define FAKE_HAL_YV12 0x32315659
#define FAKE_HAL_UNKNOWN 0x7fa30c06

static gint fake_format = FAKE_HAL_UNKNOWN;
static gint destroyed;

void
droid_media_pixel_format_constants_init (DroidMediaPixelFormatConstants * c)
{
  /* distinct values, none of them 0, so that only one entry matches */
  c->HAL_PIXEL_FORMAT_RGBA_8888 = FAKE_HAL_RGBA;
  c->HAL_PIXEL_FORMAT_RGBX_8888 = 2;
  c->HAL_PIXEL_FORMAT_RGB_888 = 3;
  c->HAL_PIXEL_FORMAT_RGB_565 = 4;
  c->HAL_PIXEL_FORMAT_BGRA_8888 = 5;
  c->HAL_PIXEL_FORMAT_YV12 = FAKE_HAL_YV12;
  c->HAL_PIXEL_FORMAT_RAW_SENSOR = 0x20;
  c->HAL_PIXEL_FORMAT_YCbCr_422_SP = 0x10;
  c->HAL_PIXEL_FORMAT_YCrCb_420_SP = 0x11;
  c->HAL_PIXEL_FORMAT_YCbCr_422_I = 0x14;
  c->QOMX_COLOR_FormatYUV420PackedSemiPlanar32m = 0x7fa30c04;
  c->QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka = 0x7fa30c03;
}

void
droid_media_buffer_get_info (DroidMediaBuffer * buffer G_GNUC_UNUSED,
    DroidMediaBufferInfo * info)
{
  memset (info, 0, sizeof (*info));
  info->width = 64;
  info->height = 48;
  info->stride = 64;
  info->format = g_atomic_int_get (&fake_format);
  info->crop_rect.right = 64;
  info->crop_rect.bottom = 48;
}

void
droid_media_buffer_destroy (DroidMediaBuffer * buffer G_GNUC_UNUSED)
{
  g_atomic_int_inc (&destroyed);
}

/* A queue buffer, for the buffer pool tests: only its user data. */
typedef struct
{
  void *user_data;
} FakeQueueBuffer;

static gint released;

void
droid_media_buffer_set_user_data (DroidMediaBuffer * buffer, void *data)
{
  ((FakeQueueBuffer *) buffer)->user_data = data;
}

void *
droid_media_buffer_get_user_data (DroidMediaBuffer * buffer)
{
  return ((FakeQueueBuffer *) buffer)->user_data;
}

void
droid_media_buffer_release (DroidMediaBuffer * buffer G_GNUC_UNUSED,
    EGLDisplay display G_GNUC_UNUSED, EGLSyncKHR fence G_GNUC_UNUSED)
{
  g_atomic_int_inc (&released);
}

/* --- g_once_init_enter/leave, for ThreadSanitizer only ---
 *
 * GLib is not built with ThreadSanitizer, so the release store in its
 * g_once_init_leave() is not seen, and every reader that takes GLib's fast
 * path looks like a race with the writer. Under ThreadSanitizer the test
 * gives its own, which ThreadSanitizer sees, in their place. */

#if defined (__SANITIZE_THREAD__)
#define DROID_TEST_TSAN 1
#elif defined (__has_feature)
#if __has_feature (thread_sanitizer)
#define DROID_TEST_TSAN 1
#endif
#endif

#ifdef DROID_TEST_TSAN
#include <pthread.h>

#define ONCE_SLOTS 256

static pthread_mutex_t once_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t once_cond = PTHREAD_COND_INITIALIZER;
static volatile void *once_running[ONCE_SLOTS];

static gint
once_slot (volatile void *location)
{
  gint i;

  for (i = 0; i < ONCE_SLOTS; i++)
    if (once_running[i] == location)
      return i;
  return -1;
}

gboolean (g_once_init_enter) (volatile void *location)
{
  gboolean first = FALSE;

  pthread_mutex_lock (&once_lock);
  while (__atomic_load_n ((gsize *) location, __ATOMIC_ACQUIRE) == 0) {
    if (once_slot (location) < 0) {
      gint free_slot = once_slot (NULL);

      g_assert (free_slot >= 0);
      once_running[free_slot] = location;
      first = TRUE;
      break;
    }
    pthread_cond_wait (&once_cond, &once_lock);
  }
  pthread_mutex_unlock (&once_lock);
  return first;
}

void (g_once_init_leave) (volatile void *location, gsize result)
{
  pthread_mutex_lock (&once_lock);
  __atomic_store_n ((gsize *) location, result, __ATOMIC_RELEASE);
  once_running[once_slot (location)] = NULL;
  pthread_cond_broadcast (&once_cond);
  pthread_mutex_unlock (&once_lock);
}

gboolean (g_once_init_enter_pointer) (void *location)
{
  return (g_once_init_enter) (location);
}

void (g_once_init_leave_pointer) (void *location, gpointer result)
{
  (g_once_init_leave) (location, (gsize) result);
}
#endif

/* --- helpers --- */

/* a buffer that holds exactly "size" bytes of "data" (and no more, for
 * AddressSanitizer to see a read past it) */
static GstBuffer *
exact_buffer (const guint8 * data, gsize size)
{
  guint8 *copy = g_malloc (size ? size : 1);

  if (size)
    memcpy (copy, data, size);
  return gst_buffer_new_wrapped_full (0, copy, size, 0, size, copy, g_free);
}

static GstDroidCodec *
codec_from_string (const gchar * caps_string, GstDroidCodecType type)
{
  GstCaps *caps = gst_caps_from_string (caps_string);
  GstDroidCodec *codec;

  fail_unless (caps != NULL);
  codec = gst_droid_codec_new_from_caps (caps, type);
  gst_caps_unref (caps);
  fail_unless (codec != NULL, "no codec for %s", caps_string);
  return codec;
}

/* --- H.265 codec_data (hvcC) --- */

/* a HEVCDecoderConfigurationRecord with no arrays, lengthSizeMinusOne 3 */
static const guint8 hvcc[23] = {
  1, 1, 0x60, 0, 0, 0, 0x90, 0, 0, 0, 0, 0, 0x5d, 0xf0, 0, 0xfc,
  0xfd, 0xf8, 0xf8, 0, 0, 0x0f, 0
};

GST_START_TEST (test_hvcc_short_is_refused)
{
  GstDroidCodec *codec = codec_from_string ("video/x-h265",
      GST_DROID_CODEC_DECODER_VIDEO);
  gsize size;

  /* byte 21 is read, so anything up to the full 23 bytes must fail
   * without being read past */
  for (size = 0; size < sizeof (hvcc); size++) {
    GstBuffer *data = exact_buffer (hvcc, size);
    GstBuffer *frame = exact_buffer (hvcc, 4);
    DroidMediaData out = { NULL, 0 };

    fail_unless_equals_int (gst_droid_codec_create_decoder_codec_data (codec,
            data, &out, frame), GST_DROID_CODEC_CODEC_DATA_ERROR);
    fail_unless (out.data == NULL);
    gst_buffer_unref (data);
    gst_buffer_unref (frame);
  }

  gst_droid_codec_unref (codec);
}

GST_END_TEST;

GST_START_TEST (test_hvcc_complete_is_taken)
{
  GstDroidCodec *codec = codec_from_string ("video/x-h265",
      GST_DROID_CODEC_DECODER_VIDEO);
  GstBuffer *data = exact_buffer (hvcc, sizeof (hvcc));
  GstBuffer *frame = exact_buffer (hvcc, 4);
  DroidMediaData out = { NULL, 0 };

  fail_unless_equals_int (gst_droid_codec_create_decoder_codec_data (codec,
          data, &out, frame), GST_DROID_CODEC_CODEC_DATA_OK);
  fail_unless_equals_int (out.size, sizeof (hvcc));
  fail_unless (memcmp (out.data, hvcc, sizeof (hvcc)) == 0);

  g_free (out.data);
  gst_buffer_unref (data);
  gst_buffer_unref (frame);
  gst_droid_codec_unref (codec);
}

GST_END_TEST;

/* --- AAC in ADTS --- */

/* an ADTS header without CRC (protection_absent = 1): AAC LC, 44.1 kHz,
 * 2 channels, then two bytes of payload */
static const guint8 adts[9] = {
  0xff, 0xf1, 0x50, 0x80, 0x01, 0x3f, 0xfc, 0xaa, 0xbb
};

#define ADTS_CAPS "audio/mpeg, mpegversion=(int)4, stream-format=(string)adts"

GST_START_TEST (test_adts_codec_data_from_short_frame)
{
  GstDroidCodec *codec = codec_from_string (ADTS_CAPS,
      GST_DROID_CODEC_DECODER_AUDIO);
  gsize size;

  for (size = 0; size < 7; size++) {
    GstBuffer *frame = exact_buffer (adts, size);
    DroidMediaData out = { NULL, 0 };

    fail_unless_equals_int (gst_droid_codec_create_decoder_codec_data (codec,
            NULL, &out, frame), GST_DROID_CODEC_CODEC_DATA_ERROR);
    gst_buffer_unref (frame);
  }

  gst_droid_codec_unref (codec);
}

GST_END_TEST;

GST_START_TEST (test_adts_codec_data_needs_syncword)
{
  GstDroidCodec *codec = codec_from_string (ADTS_CAPS,
      GST_DROID_CODEC_DECODER_AUDIO);
  guint8 bad[sizeof (adts)];
  GstBuffer *frame;
  DroidMediaData out = { NULL, 0 };

  memcpy (bad, adts, sizeof (adts));
  bad[0] = 0x00;
  frame = exact_buffer (bad, sizeof (bad));
  fail_unless_equals_int (gst_droid_codec_create_decoder_codec_data (codec,
          NULL, &out, frame), GST_DROID_CODEC_CODEC_DATA_ERROR);
  gst_buffer_unref (frame);

  gst_droid_codec_unref (codec);
}

GST_END_TEST;

GST_START_TEST (test_adts_codec_data_from_frame)
{
  GstDroidCodec *codec = codec_from_string (ADTS_CAPS,
      GST_DROID_CODEC_DECODER_AUDIO);
  GstBuffer *frame = exact_buffer (adts, sizeof (adts));
  DroidMediaData out = { NULL, 0 };

  fail_unless_equals_int (gst_droid_codec_create_decoder_codec_data (codec,
          NULL, &out, frame), GST_DROID_CODEC_CODEC_DATA_OK);
  fail_unless (out.data != NULL);
  fail_unless (out.size > 0);

  g_free (out.data);
  gst_buffer_unref (frame);
  gst_droid_codec_unref (codec);
}

GST_END_TEST;

GST_START_TEST (test_adts_frame_shorter_than_header)
{
  GstDroidCodec *codec = codec_from_string (ADTS_CAPS,
      GST_DROID_CODEC_DECODER_AUDIO);
  guint8 crc[sizeof (adts)];
  gsize size;

  /* without CRC the header is 7 bytes */
  for (size = 0; size < 7; size++) {
    GstBuffer *frame = exact_buffer (adts, size);
    DroidMediaData out = { NULL, 0 };

    fail_if (gst_droid_codec_process_decoder_data (codec, frame, &out),
        "a %" G_GSIZE_FORMAT " byte frame was taken", size);
    gst_buffer_unref (frame);
  }

  /* with CRC (protection_absent = 0) it is 9 */
  memcpy (crc, adts, sizeof (adts));
  crc[1] &= ~1;
  for (size = 2; size < 9; size++) {
    GstBuffer *frame = exact_buffer (crc, size);
    DroidMediaData out = { NULL, 0 };

    fail_if (gst_droid_codec_process_decoder_data (codec, frame, &out),
        "a %" G_GSIZE_FORMAT " byte frame with CRC was taken", size);
    gst_buffer_unref (frame);
  }

  gst_droid_codec_unref (codec);
}

GST_END_TEST;

GST_START_TEST (test_adts_frame_payload)
{
  GstDroidCodec *codec = codec_from_string (ADTS_CAPS,
      GST_DROID_CODEC_DECODER_AUDIO);
  GstBuffer *frame = exact_buffer (adts, sizeof (adts));
  GstBuffer *header_only = exact_buffer (adts, 7);
  DroidMediaData out = { NULL, 0 };

  fail_unless (gst_droid_codec_process_decoder_data (codec, frame, &out));
  fail_unless_equals_int (out.size, 2);
  fail_unless_equals_int (((guint8 *) out.data)[0], 0xaa);
  fail_unless_equals_int (((guint8 *) out.data)[1], 0xbb);
  g_free (out.data);

  /* a header with no payload gives no data, not a copy into NULL */
  fail_unless (gst_droid_codec_process_decoder_data (codec, header_only,
          &out));
  fail_unless_equals_int (out.size, 0);
  g_free (out.data);

  gst_buffer_unref (frame);
  gst_buffer_unref (header_only);
  gst_droid_codec_unref (codec);
}

GST_END_TEST;

GST_START_TEST (test_raw_aac_empty_frame)
{
  GstDroidCodec *codec =
      codec_from_string
      ("audio/mpeg, mpegversion=(int)4, stream-format=(string)raw",
      GST_DROID_CODEC_DECODER_AUDIO);
  GstBuffer *frame = exact_buffer (adts, 0);
  DroidMediaData out = { NULL, 0 };

  fail_unless (gst_droid_codec_process_decoder_data (codec, frame, &out));
  fail_unless_equals_int (out.size, 0);
  g_free (out.data);

  gst_buffer_unref (frame);
  gst_droid_codec_unref (codec);
}

GST_END_TEST;

/* --- the media buffer allocator --- */

static GstVideoFormat
format_of (GstMemory * mem)
{
  GstVideoInfo *info = gst_droid_media_buffer_get_video_info (mem);

  fail_unless (info != NULL);
  return GST_VIDEO_INFO_FORMAT (info);
}

GST_START_TEST (test_media_buffer_unknown_format)
{
  GstAllocator *allocator = gst_droid_media_buffer_allocator_new ();
  GstMemory *mem;

  /* a vendor format the table does not have: looked up past the table's
   * end before */
  g_atomic_int_set (&fake_format, FAKE_HAL_UNKNOWN);
  mem = gst_droid_media_buffer_allocator_alloc_from_buffer (allocator,
      (DroidMediaBuffer *) 0x1);
  fail_unless (mem != NULL);
  fail_unless_equals_int (format_of (mem), GST_VIDEO_FORMAT_ENCODED);
  gst_memory_unref (mem);

  gst_object_unref (allocator);
}

GST_END_TEST;

GST_START_TEST (test_media_buffer_known_formats)
{
  GstAllocator *allocator = gst_droid_media_buffer_allocator_new ();
  GstVideoInfo *info;
  GstMemory *mem;

  g_atomic_int_set (&fake_format, FAKE_HAL_RGBA);
  mem = gst_droid_media_buffer_allocator_alloc_from_buffer (allocator,
      (DroidMediaBuffer *) 0x1);
  fail_unless (mem != NULL);
  fail_unless_equals_int (format_of (mem), GST_VIDEO_FORMAT_RGBA);
  gst_memory_unref (mem);

  /* YV12 gets its chroma strides aligned */
  g_atomic_int_set (&fake_format, FAKE_HAL_YV12);
  mem = gst_droid_media_buffer_allocator_alloc_from_buffer (allocator,
      (DroidMediaBuffer *) 0x1);
  fail_unless (mem != NULL);
  fail_unless_equals_int (format_of (mem), GST_VIDEO_FORMAT_YV12);
  info = gst_droid_media_buffer_get_video_info (mem);
  fail_unless_equals_int (GST_VIDEO_INFO_PLANE_STRIDE (info, 1), 32);
  fail_unless_equals_int (GST_VIDEO_INFO_PLANE_STRIDE (info, 2), 32);
  gst_memory_unref (mem);

  gst_object_unref (allocator);
}

GST_END_TEST;

/* Decoders look formats up for every frame, from their own threads: the
 * table must be filled once, not rewritten while others read it
 * (ThreadSanitizer sees the latter). */
#define LOOKUP_THREADS 4

static gpointer
look_up_formats (gpointer data)
{
  GstAllocator *allocator = data;
  gint i;

  for (i = 0; i < 200; i++) {
    GstMemory *mem =
        gst_droid_media_buffer_allocator_alloc_from_buffer (allocator,
        (DroidMediaBuffer *) 0x1);
    fail_unless (mem != NULL);
    gst_memory_unref (mem);
  }
  return NULL;
}

GST_START_TEST (test_media_buffer_threads)
{
  GstAllocator *allocator = gst_droid_media_buffer_allocator_new ();
  GThread *threads[LOOKUP_THREADS];
  gint i;

  g_atomic_int_set (&fake_format, FAKE_HAL_RGBA);
  for (i = 0; i < LOOKUP_THREADS; i++)
    threads[i] = g_thread_new ("lookup", look_up_formats, allocator);
  for (i = 0; i < LOOKUP_THREADS; i++)
    g_thread_join (threads[i]);

  gst_object_unref (allocator);
}

GST_END_TEST;

/* --- the buffer pool and droidmedia's queue buffers ---
 *
 * droidmedia hands over one reference to a queue buffer, with
 * buffer_created, and expects one droid_media_buffer_destroy () for it. */

static GstBufferPool *
queue_pool_new (void)
{
  GstBufferPool *pool = gst_droid_buffer_pool_new ();
  GstStructure *config = gst_buffer_pool_get_config (pool);
  GstCaps *caps = gst_caps_from_string ("video/x-raw("
      GST_CAPS_FEATURE_MEMORY_DROID_MEDIA_QUEUE_BUFFER
      "), format=RGBA, width=64, height=48, framerate=0/1");

  gst_buffer_pool_config_set_params (config, caps, 1, 0, 0);
  gst_caps_unref (caps);
  fail_unless (gst_buffer_pool_set_config (pool, config));
  fail_unless (gst_buffer_pool_set_active (pool, TRUE));
  return pool;
}

static void
queue_pool_free (GstBufferPool * pool)
{
  fail_unless (gst_buffer_pool_set_active (pool, FALSE));
  gst_object_unref (pool);
}

/* A frame for a buffer bound in one pool, taken from another after the
 * decoder renegotiated: a second memory for the buffer was made then, and
 * the buffer was destroyed twice - once for the reference the client was
 * given and once for droidmedia's own, which freed it while its queue slot
 * still pointed at it. */
GST_START_TEST (test_pool_rebind_keeps_one_reference)
{
  FakeQueueBuffer qb = { NULL };
  DroidMediaBuffer *dbuf = (DroidMediaBuffer *) & qb;
  GstBufferPool *first, *second;
  GstBuffer *buffer;

  g_atomic_int_set (&fake_format, FAKE_HAL_RGBA);
  g_atomic_int_set (&destroyed, 0);
  g_atomic_int_set (&released, 0);
  first = queue_pool_new ();
  second = queue_pool_new ();

  /* buffer_created, then a frame and its release, in the first pool */
  fail_unless (gst_droid_buffer_pool_bind_media_buffer (first, dbuf));
  buffer = gst_droid_buffer_pool_acquire_media_buffer (first, dbuf);
  fail_unless (buffer != NULL);
  gst_buffer_unref (buffer);
  fail_unless_equals_int (g_atomic_int_get (&released), 1);

  /* the next frame, after a new pool was negotiated */
  buffer = gst_droid_buffer_pool_acquire_media_buffer (second, dbuf);
  fail_unless (buffer != NULL);
  fail_unless (buffer->pool == second);
  fail_unless (gst_droid_media_buffer_memory_get_buffer_from_gst_buffer
      (buffer) == dbuf);
  fail_unless_equals_int (g_atomic_int_get (&destroyed), 0);
  gst_buffer_unref (buffer);
  fail_unless_equals_int (g_atomic_int_get (&released), 2);

  /* the queue goes: each pool lets go of what it holds */
  gst_droid_buffer_pool_media_buffers_invalidated (second);
  gst_droid_buffer_pool_media_buffers_invalidated (first);
  fail_unless_equals_int (g_atomic_int_get (&destroyed), 1);
  fail_unless (qb.user_data == NULL);

  queue_pool_free (second);
  queue_pool_free (first);
}

GST_END_TEST;

/* A buffer whose memory went with an invalidation has no reference left to
 * bind: the frame is not taken, and nothing is destroyed for it. */
GST_START_TEST (test_pool_unbound_buffer_is_not_taken)
{
  FakeQueueBuffer qb = { NULL };
  DroidMediaBuffer *dbuf = (DroidMediaBuffer *) & qb;
  GstBufferPool *pool;

  g_atomic_int_set (&fake_format, FAKE_HAL_RGBA);
  g_atomic_int_set (&destroyed, 0);
  pool = queue_pool_new ();

  fail_unless (gst_droid_buffer_pool_acquire_media_buffer (pool, dbuf) == NULL);
  gst_droid_buffer_pool_media_buffers_invalidated (pool);
  fail_unless_equals_int (g_atomic_int_get (&destroyed), 0);

  queue_pool_free (pool);
}

GST_END_TEST;

static Suite *
droid_suite (void)
{
  Suite *s = suite_create ("droid");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_hvcc_short_is_refused);
  tcase_add_test (tc, test_hvcc_complete_is_taken);
  tcase_add_test (tc, test_adts_codec_data_from_short_frame);
  tcase_add_test (tc, test_adts_codec_data_needs_syncword);
  tcase_add_test (tc, test_adts_codec_data_from_frame);
  tcase_add_test (tc, test_adts_frame_shorter_than_header);
  tcase_add_test (tc, test_adts_frame_payload);
  tcase_add_test (tc, test_raw_aac_empty_frame);
  tcase_add_test (tc, test_media_buffer_unknown_format);
  tcase_add_test (tc, test_media_buffer_known_formats);
  tcase_add_test (tc, test_media_buffer_threads);
  tcase_add_test (tc, test_pool_rebind_keeps_one_reference);
  tcase_add_test (tc, test_pool_unbound_buffer_is_not_taken);

  return s;
}

GST_CHECK_MAIN (droid);
