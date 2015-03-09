lib_LTLIBRARIES = libgstdroid-@GST_API_VERSION@.la
libgstdroid_@GST_API_VERSION@_ladir = $(libdir)

libgstdroid_@GST_API_VERSION@_la_includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/allocators

libgstdroid_@GST_API_VERSION@_la_CFLAGS = $(GST_CFLAGS) -I$(top_builddir)/gst-libs/ext/droidmedia/

libgstdroid_@GST_API_VERSION@_la_LIBADD = $(GST_LIBS)

noinst_HEADERS =

libgstdroid_@GST_API_VERSION@_la_SOURCES = \
	gstwrappedmemory.c \
	gstdroidmediabuffer.c \
	gstdroidbufferpool.c \
	$(top_builddir)/gst-libs/ext/droidmedia/hybris.c

libgstdroid_@GST_API_VERSION@_la_include_HEADERS = \
	gstwrappedmemory.h \
	gstdroidmediabuffer.h \
	gstdroidbufferpool.h
