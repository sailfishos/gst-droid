SUBDIRS = memory utils

lib_LTLIBRARIES = libgstdroidutils-@GST_API_VERSION@.la

libgstdroidutils_@GST_API_VERSION@_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/memory/libgstmemory.la \
	$(top_builddir)/gst-libs/gst/utils/libgstutils.la

# this is needed by autofoo
libgstdroidutils_@GST_API_VERSION@_la_SOURCES =
