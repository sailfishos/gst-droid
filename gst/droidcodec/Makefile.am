plugin_LTLIBRARIES = libgstdroidcodec.la
libgstdroidcodec_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) $(DROID_CFLAGS) \
			       -I$(top_builddir)/gst-libs/ \
			       -I$(top_builddir)/inc/omx/ \
                               -I$(top_builddir)/inc/hybris/ \
                               -I$(top_builddir)/inc/android/

libgstdroidcodec_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) $(DROID_LIBS) \
                             $(top_builddir)/gst-libs/gst/memory/libgstdroidmemory-@GST_API_VERSION@.la
libgstdroidcodec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdroidcodec_la_LIBTOOLFLAGS = --tag=disable-static

libgstdroidcodec_la_SOURCES = \
	plugin.c \
	gstdroiddec.c \
	gstdroidenc.c \
	gstdroidcodec.c \
	gstdroidcodectype.c \
	mappings.c \
	gstdroidcodecallocatoromx.c \
	gstdroidcodecallocatorgralloc.c \
	gstencoderparams.c

noinst_HEADERS = \
	plugin.h \
	gstdroiddec.h \
	gstdroidenc.h \
	gstdroidcodec.h \
	gstdroidcodectype.h \
	gstdroidcodecallocatoromx.h \
	gstdroidcodecallocatorgralloc.h \
	gstencoderparams.h
