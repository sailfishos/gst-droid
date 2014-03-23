noinst_PROGRAMS = test_gralloc_allocator
test_gralloc_allocator_CFLAGS = $(GST_CFLAGS) -I$(top_builddir)/gst-libs/gst/memory/
test_gralloc_allocator_LDADD = $(GST_LIBS) $(top_builddir)/gst-libs/gst/memory/libgstdroidmemory-@GST_API_VERSION@.la
test_gralloc_allocator_SOURCES = allocator.c
test_gralloc_allocator_LDFLAGS = -Wl,--as-needed
