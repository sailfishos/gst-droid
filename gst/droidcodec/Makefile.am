AM_CPPFLAGS = -DSYSCONFDIR=\"$(sysconfdir)\"
noinst_LTLIBRARIES = libgstdroidcodec.la
libgstdroidcodec_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) $(DROID_CFLAGS) $(ORC_CFLAGS) \
			     -I$(top_builddir)/gst/ \
			     -I$(top_builddir)/gst-libs/ \
			     -I$(top_builddir)/gst-libs/ext/droidmedia/

libgstdroidcodec_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) $(DROID_LIBS) $(ORC_LIBS)

libgstdroidcodec_la_SOURCES = \
	gstdroiddec.c \
	gstdroidenc.c \
	gstdroidcodec.c

noinst_HEADERS = \
	gstdroiddec.h \
	gstdroidenc.h \
	gstdroidcodec.h
