lib_LTLIBRARIES = libgstdroidmemory-@GST_API_VERSION@.la
libgstdroidmemory_@GST_API_VERSION@_ladir = $(libdir)

libgstdroidmemory_@GST_API_VERSION@_la_includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/allocators

libgstdroidmemory_@GST_API_VERSION@_la_CFLAGS = $(GST_CFLAGS) $(DROID_CFLAGS) $(HARDWARE_CFLAGS)
libgstdroidmemory_@GST_API_VERSION@_la_LIBADD = $(GST_LIBS) $(DROID_LIBS) $(HARDWARE_LIBS)

noinst_HEADERS =

libgstdroidmemory_@GST_API_VERSION@_la_SOURCES = \
	gstgralloc.c \
	gstwrappedmemory.c

libgstdroidmemory_@GST_API_VERSION@_la_include_HEADERS = \
	gstgralloc.h \
	gstwrappedmemory.h
