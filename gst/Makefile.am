# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgstdroid.la

# sources used to compile this plug-in
libgstdroid_la_SOURCES = plugin.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstdroid_la_CFLAGS = $(GST_CFLAGS)
libgstdroid_la_LIBADD = $(GST_LIBS)
libgstdroid_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdroid_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = plugin.h
