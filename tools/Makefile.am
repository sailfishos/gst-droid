AM_CFLAGS = $(GST_CFLAGS) -DSYSCONFDIR=\"$(sysconfdir)\"

noinst_LTLIBRARIES = libcommon.la
libcommon_la_SOURCES = common.c
libcommon_la_LIBADD = $(GST_LIBS) $(GSTPB_LIBS)

noinst_HEADERS = common.h

bin_PROGRAMS = \
	record-video \
	dump-resolutions

record_video_SOURCES = record.c
record_video_LDADD = libcommon.la

dump_resolutions_SOURCES = resolutions.c
dump_resolutions_LDADD = libcommon.la
