noinst_LTLIBRARIES = libgstdroideglsink.la
libgstdroideglsink_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) \
			       -I$(top_builddir)/gst-libs/ \
			       -I/usr/include/droidmedia/

libgstdroideglsink_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS)

libgstdroideglsink_la_SOURCES = gstdroideglsink.c
noinst_HEADERS = gstdroideglsink.h
