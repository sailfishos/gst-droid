AM_CPPFLAGS = -DSYSCONFDIR=\"$(sysconfdir)\"
noinst_LTLIBRARIES = libgstdroidcodec.la
libgstdroidcodec_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) $(DROID_CFLAGS) \
			     -I$(top_srcdir)/gst/ \
			     -I$(top_srcdir)/gst-libs/ \
			     -I$(top_srcdir)/gst-libs/ext/droidmedia/

libgstdroidcodec_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) $(DROID_LIBS)

libgstdroidcodec_la_SOURCES = \
	gstdroiddec.c \
	gstdroidenc.c \
	gstdroidcodec.c

noinst_HEADERS = \
	gstdroiddec.h \
	gstdroidenc.h \
	gstdroidcodec.h
