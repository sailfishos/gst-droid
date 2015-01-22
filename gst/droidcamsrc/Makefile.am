AM_CPPFLAGS = -DSYSCONFDIR=\"$(sysconfdir)\"
noinst_LTLIBRARIES = libgstdroidcamsrc.la
libgstdroidcamsrc_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) \
                              $(DROID_CFLAGS) $(NGM_CFLAGS) $(EXIF_CFLAGS) \
			      -I$(top_builddir)/gst/ \
			      -I$(top_builddir)/gst-libs/ \
                              -I$(top_builddir)/inc/

libgstdroidcamsrc_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) \
                              $(DROID_LIBS) $(NGM_LIBS) $(EXIF_LIBS) \
                              $(top_builddir)/gst-libs/gst/memory/libgstdroidmemory-@GST_API_VERSION@.la \
                              -lgstbasecamerabinsrc-1.0 \
                              -lgstphotography-1.0

libgstdroidcamsrc_la_SOURCES = \
	gstdroidcamsrc.c \
	gstdroidcamsrcdev.c \
	gstdroidcamsrcparams.c \
	gstdroidcamsrcenums.c \
	gstdroidcamsrcphotography.c \
	gstdroidcamsrcquirks.c \
	gstdroidcamsrcexif.c

noinst_HEADERS = \
	gstdroidcamsrc.h \
	gstdroidcamsrcdev.h \
	gstdroidcamsrcparams.h \
	gstdroidcamsrcenums.h \
	gstdroidcamsrcphotography.h \
	gstdroidcamsrcquirks.h \
	gstdroidcamsrcexif.h
