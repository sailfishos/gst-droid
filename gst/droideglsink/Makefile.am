noinst_LTLIBRARIES = libgstdroideglsink.la
libgstdroideglsink_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) \
			       -I$(top_srcdir)/gst-libs/ \
			       -I$(top_srcdir)/gst-libs/ext/droidmedia/

libgstdroideglsink_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) \
			       $(top_builddir)/gst-libs/gst/memory/libgstdroidmemory-@GST_API_VERSION@.la

libgstdroideglsink_la_SOURCES = gstdroideglsink.c
noinst_HEADERS = gstdroideglsink.h
