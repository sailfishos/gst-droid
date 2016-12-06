AM_CPPFLAGS = -DSYSCONFDIR=\"$(sysconfdir)\"
noinst_LTLIBRARIES = libgstdroidcamsrc.la
libgstdroidcamsrc_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) \
                              $(NGM_CFLAGS) $(EXIF_CFLAGS) \
			      -I$(top_builddir)/gst/ \
			      -I$(top_builddir)/gst-libs/ \
			      -I/usr/include/droidmedia/

libgstdroidcamsrc_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) \
                              $(NGM_LIBS) $(EXIF_LIBS) \
                              -lgstbasecamerabinsrc-1.0 \
                              -lgstphotography-1.0

libgstdroidcamsrc_la_SOURCES = \
	gstdroidcamsrc.c \
	gstdroidcamsrcdev.c \
	gstdroidcamsrcparams.c \
	gstdroidcamsrcenums.c \
	gstdroidcamsrcphotography.c \
	gstdroidcamsrcquirks.c \
	gstdroidcamsrcexif.c \
	gstdroidcamsrcmode.c \
        gstdroidcamsrcrecorder.c

noinst_HEADERS = \
	gstdroidcamsrc.h \
	gstdroidcamsrcdev.h \
	gstdroidcamsrcparams.h \
	gstdroidcamsrcenums.h \
	gstdroidcamsrcphotography.h \
	gstdroidcamsrcquirks.h \
	gstdroidcamsrcexif.h \
	gstdroidcamsrcmode.h \
        gstdroidcamsrcrecorder.h
