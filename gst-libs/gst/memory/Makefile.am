noinst_LTLIBRARIES = libgstmemory.la

libgstmemory_la_includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/allocators

libgstmemory_la_CFLAGS = \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs/ext/droidmedia/

libgstmemory_la_LIBADD = \
	$(GST_LIBS)

libgstmemory_la_SOURCES = \
	gstwrappedmemory.c \
	gstdroidmediabuffer.c \
	$(top_builddir)/gst-libs/ext/droidmedia/hybris.c

libgstmemory_la_include_HEADERS = \
	gstwrappedmemory.h \
	gstdroidmediabuffer.h
