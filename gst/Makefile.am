AM_CPPFLAGS = -DSYSCONFDIR=\"$(sysconfdir)\"

SUBDIRS = droideglsink droidcodec droidcamsrc

plugin_LTLIBRARIES = libgstdroid.la

libgstdroid_la_CFLAGS = $(GST_CFLAGS) $(NGI_CFLAGS) $(EGL_CFLAGS) \
                        $(NGM_CFLAGS)\
	                -I$(top_builddir)/gst-libs/ \
			-I/usr/include/droidmedia/ \
                        -Idroidcamsrc/ \
                        -Idroideglsink/ \
                        -Idroidcodec/

libgstdroid_la_LIBADD = $(GST_LIBS) $(NGI_LIBS) $(EGL_LIBS) \
                        $(NGM_LIBS) \
                        $(top_builddir)/gst-libs/gst/droid/libgstdroid-@GST_API_VERSION@.la \
                        -lgstbasecamerabinsrc-1.0 \
                        -lgstphotography-1.0 \
                        droideglsink/libgstdroideglsink.la \
                        droidcamsrc/libgstdroidcamsrc.la \
                        droidcodec/libgstdroidcodec.la

libgstdroid_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstdroid_la_SOURCES = plugin.c \
			 /usr/share/droidmedia/hybris.c

noinst_HEADERS = plugin.h
