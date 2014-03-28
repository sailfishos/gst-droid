plugin_LTLIBRARIES = libgstdroidcodec.la
libgstdroidcodec_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) \
			       -I$(top_builddir)/gst-libs/

libgstdroidcodec_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) \
			       $(top_builddir)/gst-libs/gst/memory/libgstdroidmemory-@GST_API_VERSION@.la
libgstdroidcodec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdroidcodec_la_LIBTOOLFLAGS = --tag=disable-static

libgstdroidcodec_la_SOURCES = gstdroiddec.c
noinst_HEADERS = gstdroiddec.h
