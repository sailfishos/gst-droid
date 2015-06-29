AM_CPPFLAGS = -DSYSCONFDIR=\"$(sysconfdir)\"
bin_PROGRAMS = mk-cam-conf
mk_cam_conf_CFLAGS = \
			$(GST_CFLAGS) \
			-I$(top_builddir)/ \
			-I$(top_builddir)/gst-libs/ \
			-I$(top_builddir)/gst/ \
			-I$(top_builddir)/gst/droidcamsrc/ \
			-I$(top_builddir)/gst/droideglsink/ \
			-I$(top_builddir)/gst/droidcodec/ \
			-I/usr/include/droidmedia/

mk_cam_conf_LDADD = \
			$(GST_LIBS) \
                        $(top_builddir)/gst-libs/gst/droid/libgstdroid-@GST_API_VERSION@.la \
                        $(top_builddir)/gst/droideglsink/libgstdroideglsink.la \
                        $(top_builddir)/gst/droidcamsrc/libgstdroidcamsrc.la \
                        $(top_builddir)/gst/droidcodec/libgstdroidcodec.la

mk_cam_conf_SOURCES = \
			gstdroidcamsrcconf.c \
			$(top_builddir)/gst/plugin.c \
			/usr/share/droidmedia/hybris.c