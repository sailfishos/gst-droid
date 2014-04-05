plugin_LTLIBRARIES = libgstdroidcamsrc.la
libgstdroidcamsrc_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) $(DROID_CFLAGS) \
			       -I$(top_builddir)/gst-libs/ \
                               -I$(top_builddir)/inc/hybris/

libgstdroidcamsrc_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) $(DROID_LIBS) \
                              $(top_builddir)/gst-libs/gst/memory/libgstdroidmemory-@GST_API_VERSION@.la
libgstdroidcamsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdroidcamsrc_la_LIBTOOLFLAGS = --tag=disable-static

libgstdroidcamsrc_la_SOURCES = \
	plugin.c \
	gstdroidcamsrc.c

noinst_HEADERS = \
	plugin.h \
	gstdroidcamsrc.h
