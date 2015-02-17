noinst_LTLIBRARIES = libgstdroideglsink.la
libgstdroideglsink_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) \
			       -I$(top_builddir)/gst-libs/ \
			       -I$(top_builddir)/gst-libs/ext/droidmedia/

libgstdroideglsink_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) \
			       $(top_builddir)/gst-libs/gst/memory/libgstdroidmemory-@GST_API_VERSION@.la

libgstdroideglsink_la_SOURCES = gstdroideglsink.c
noinst_HEADERS = gstdroideglsink.h
