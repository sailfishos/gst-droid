#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include "gstgralloc.h"

GST_START_TEST (test_create_destroy)
{
  GstAllocator *allocator = gst_gralloc_allocator_new();

  fail_unless(allocator != NULL);

  gst_object_unref(GST_OBJECT(allocator));
}
GST_END_TEST;

GST_START_TEST (test_create_alloc_destroy)
{
  GstAllocator *allocator = gst_gralloc_allocator_new();

  fail_unless(allocator != NULL);

  GstMemory *mem = gst_gralloc_allocator_alloc (allocator, 640, 480, 0x32315659, GST_GRALLOC_USAGE_SW_READ_MASK | GST_GRALLOC_USAGE_SW_WRITE_MASK | GST_GRALLOC_USAGE_HW_TEXTURE);

  fail_unless(mem != NULL);

  ASSERT_CAPS_REFCOUNT(allocator, "allocator", 2);
  gst_memory_unref(mem);

  gst_object_unref(GST_OBJECT(allocator));
}
GST_END_TEST;

GST_START_TEST (test_memory)
{
  GstAllocator *allocator = gst_gralloc_allocator_new();

  fail_unless(allocator != NULL);

  GstMemory *mem = gst_gralloc_allocator_alloc (allocator, 640, 480, 0x32315659, GST_GRALLOC_USAGE_SW_READ_MASK | GST_GRALLOC_USAGE_SW_WRITE_MASK | GST_GRALLOC_USAGE_HW_TEXTURE);

  fail_unless(mem != NULL);
  fail_unless(gst_memory_get_native_buffer (mem) != NULL);
  fail_unless_equals_int (gst_is_gralloc_memory(mem), TRUE);
  fail_unless_equals_int (GST_MEMORY_IS_READONLY(mem), FALSE);
  fail_unless_equals_int (GST_MEMORY_IS_NO_SHARE(mem), TRUE);
  fail_unless_equals_int (GST_MEMORY_IS_ZERO_PADDED(mem), FALSE);
  fail_unless_equals_int (GST_MEMORY_IS_ZERO_PREFIXED(mem), FALSE);
  fail_unless_equals_int (GST_MEMORY_IS_PHYSICALLY_CONTIGUOUS(mem), FALSE);
  fail_unless_equals_int (GST_MEMORY_IS_NOT_MAPPABLE(mem), TRUE);

  gsize size, offset, maxsize;
  size = gst_memory_get_sizes (mem, &offset, &maxsize);

  fail_unless_equals_int (size, -1);
  fail_unless_equals_int (offset, 0);
  fail_unless_equals_int (maxsize, -1);

  gst_memory_unref(mem);

  gst_object_unref(GST_OBJECT(allocator));
}
GST_END_TEST;

GST_START_TEST (test_wrap)
{
  GstAllocator *allocator = gst_gralloc_allocator_new();

  fail_unless(allocator != NULL);

  gsize size = 16 * 16;
  guint8 data[size];

  GstMemory *mem = gst_gralloc_allocator_wrap (allocator, 16, 16,
					       GST_GRALLOC_USAGE_HW_TEXTURE,
					       data, size, GST_VIDEO_FORMAT_I420);

  fail_unless(mem == NULL);

  mem = gst_gralloc_allocator_wrap (allocator, 16, 16, GST_GRALLOC_USAGE_HW_TEXTURE,
				    data, size, GST_VIDEO_FORMAT_NV12);

  fail_unless(mem != NULL);

  gst_memory_unref(mem);

  gst_object_unref(GST_OBJECT(allocator));
}
GST_END_TEST;

static Suite *
allocator_suite (void)
{
  Suite *s = suite_create ("gralloc allocator");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_create_destroy);
  tcase_add_test (tc_chain, test_create_alloc_destroy);
  tcase_add_test (tc_chain, test_memory);
  tcase_add_test (tc_chain, test_wrap);

  return s;
}

GST_CHECK_MAIN (allocator);
