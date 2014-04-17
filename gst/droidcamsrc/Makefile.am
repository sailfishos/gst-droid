plugin_LTLIBRARIES = libgstdroidcamsrc.la
libgstdroidcamsrc_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) \
                              $(DROID_CFLAGS) $(NGM_CFLAGS)\
			      -I$(top_builddir)/gst-libs/ \
                              -I$(top_builddir)/inc/hybris/

libgstdroidcamsrc_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) \
                              $(DROID_LIBS) $(NGM_LIBS) \
                              $(top_builddir)/gst-libs/gst/memory/libgstdroidmemory-@GST_API_VERSION@.la \
                              -lgstbasecamerabinsrc-1.0 \
                              -lgstphotography-1.0

libgstdroidcamsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdroidcamsrc_la_LIBTOOLFLAGS = --tag=disable-static

libgstdroidcamsrc_la_SOURCES = \
	plugin.c \
	gstdroidcamsrc.c \
	gstdroidcamsrcdev.c \
	gstdroidcamsrcdevmemory.c \
	gstdroidcamsrcbufferpool.c \
	gstdroidcamsrcstreamwindow.c \
	gstdroidcamsrcparams.c \
	gstdroidcamsrcenums.c \
	gstdroidcamsrcphotography.c

noinst_HEADERS = \
	plugin.h \
	gstdroidcamsrc.h \
	gstdroidcamsrcdev.h \
	gstdroidcamsrcdevmemory.h \
	gstdroidcamsrcbufferpool.h \
	gstdroidcamsrcstreamwindow.h \
	gstdroidcamsrcparams.h \
	gstdroidcamsrcenums.h \
	gstdroidcamsrcphotography.h
