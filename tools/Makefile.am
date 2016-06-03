AM_CFLAGS = $(GST_CFLAGS) -DSYSCONFDIR=\"$(sysconfdir)\"

noinst_LTLIBRARIES = libcommon.la
libcommon_la_SOURCES = common.c
libcommon_la_LIBADD = $(GST_LIBS) $(GSTPB_LIBS)

noinst_HEADERS = common.h

bin_PROGRAMS = \
	mk-cam-conf \
	record-video \
	dump-resolutions

mk_cam_conf_SOURCES = gstdroidcamsrcconf.c
mk_cam_conf_LDADD = libcommon.la

record_video_SOURCES = record.c
record_video_LDADD = libcommon.la

dump_resolutions_SOURCES = resolutions.c
dump_resolutions_LDADD = libcommon.la
