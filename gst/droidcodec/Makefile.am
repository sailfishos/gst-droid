AM_CPPFLAGS = -DSYSCONFDIR=\"$(sysconfdir)\"
noinst_LTLIBRARIES = libgstdroidcodec.la
libgstdroidcodec_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) $(DROID_CFLAGS) \
			      -I$(top_builddir)/gst/ \
			       -I$(top_builddir)/gst-libs/ \
			       -I$(top_builddir)/inc/omx/ \
                               -I$(top_builddir)/inc/hybris/ \
                               -I$(top_builddir)/inc/android/

libgstdroidcodec_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) $(DROID_LIBS) \
                             $(top_builddir)/gst-libs/gst/memory/libgstdroidmemory-@GST_API_VERSION@.la

libgstdroidcodec_la_SOURCES = \
	gstdroiddec.c \
	gstdroidenc.c \
	gstdroidcodec.c \
	gstdroidcodectype.c \
	mappings.c \
	gstdroidcodecallocatoromx.c \
	gstdroidcodecallocatorgralloc.c \
	gstencoderparams.c

noinst_HEADERS = \
	gstdroiddec.h \
	gstdroidenc.h \
	gstdroidcodec.h \
	gstdroidcodectype.h \
	gstdroidcodecallocatoromx.h \
	gstdroidcodecallocatorgralloc.h \
	gstencoderparams.h
