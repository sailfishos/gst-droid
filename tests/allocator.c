#include <gst/gst.h>
#include "gstgralloc.h"
#include <assert.h>

int
main(int argc, char *argv[]) {
  gst_init(&argc, &argv);

  GstAllocator *allocator = gst_gralloc_allocator_new();
  assert (allocator != NULL);

  gst_object_unref(GST_OBJECT(allocator));

  allocator = gst_gralloc_allocator_new();
  GstMemory *mem = gst_gralloc_allocator_alloc (allocator, 640, 480, 0x32315659, GST_GRALLOC_USAGE_SW_READ_MASK | GST_GRALLOC_USAGE_SW_WRITE_MASK | GST_GRALLOC_USAGE_HW_TEXTURE);

  assert(mem != NULL);

  assert(gst_is_gralloc_memory(mem) == TRUE);
  assert(GST_MEMORY_IS_READONLY(mem) == FALSE);
  assert(GST_MEMORY_IS_NO_SHARE(mem) == TRUE);
  assert(GST_MEMORY_IS_ZERO_PADDED(mem) == FALSE);
  assert(GST_MEMORY_IS_ZERO_PREFIXED(mem) == FALSE);
  assert(GST_MEMORY_IS_PHYSICALLY_CONTIGUOUS(mem) == FALSE);
  assert(GST_MEMORY_IS_NOT_MAPPABLE(mem) == TRUE);

  gsize size, offset, maxsize;
  size = gst_memory_get_sizes (mem, &offset, &maxsize);

  assert(size == -1);
  assert(offset == 0);
  assert(maxsize == -1);

  gst_memory_unref(mem);
  gst_object_unref(GST_OBJECT(allocator));

  gst_deinit();

  return 0;
}
