noinst_PROGRAMS = test_gralloc_allocator
AM_CFLAGS = $(GST_CFLAGS) $(CHECK_CFLAGS) -I$(top_builddir)/gst-libs/gst/memory/
LDADD = $(GST_LIBS) $(CHECK_LIBS) $(top_builddir)/gst-libs/gst/memory/libgstdroidmemory-@GST_API_VERSION@.la
test_gralloc_allocator_SOURCES = allocator.c
AM_LDFLAGS = -Wl,--as-needed
