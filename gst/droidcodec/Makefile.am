AM_CPPFLAGS = -DSYSCONFDIR=\"$(sysconfdir)\"
noinst_LTLIBRARIES = libgstdroidcodec.la
libgstdroidcodec_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) $(DROID_CFLAGS) $(ORC_CFLAGS) \
			     -I$(top_builddir)/gst/ \
			     -I$(top_builddir)/gst-libs/ \
			     -I/usr/include/droidmedia/

libgstdroidcodec_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) $(DROID_LIBS) $(ORC_LIBS)

libgstdroidcodec_la_SOURCES = \
	gstdroidvdec.c \
	gstdroidvenc.c \
	gstdroidadec.c \
	gstdroidaenc.c

noinst_HEADERS = \
	gstdroidvdec.h \
	gstdroidvenc.h \
	gstdroidadec.h \
	gstdroidaenc.h
