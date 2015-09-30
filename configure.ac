dnl required version of autoconf
AC_PREREQ([2.53])

dnl TODO: fill in your package name and package version here
AC_INIT([gst-droid],[1.0.0])

dnl required versions of gstreamer and plugins-base
GST_REQUIRED=1.0.0
GSTPB_REQUIRED=1.0.0

AC_CONFIG_SRCDIR([gst/plugin.c])
AC_CONFIG_HEADERS([config.h])

dnl required version of automake
AM_INIT_AUTOMAKE([1.10])

dnl enable mainainer mode by default
AM_MAINTAINER_MODE([enable])

dnl check for tools (compiler etc.)
AC_PROG_CC

dnl required version of libtool
LT_PREREQ([2.2.6])
LT_INIT

dnl give error and exit if we don't have pkgconfig
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, [ ], [
  AC_MSG_ERROR([You need to have pkg-config installed!])
])

dnl Check for the required version of GStreamer core (and gst-plugins-base)
dnl This will export GST_CFLAGS and GST_LIBS variables for use in Makefile.am
dnl
dnl If you need libraries from gst-plugins-base here, also add:
dnl for libgstaudio-1.0: gstreamer-audio-1.0 >= $GST_REQUIRED
dnl for libgstvideo-1.0: gstreamer-video-1.0 >= $GST_REQUIRED
dnl for libgsttag-1.0: gstreamer-tag-1.0 >= $GST_REQUIRED
dnl for libgstpbutils-1.0: gstreamer-pbutils-1.0 >= $GST_REQUIRED
dnl for libgstfft-1.0: gstreamer-fft-1.0 >= $GST_REQUIRED
dnl for libgstinterfaces-1.0: gstreamer-interfaces-1.0 >= $GST_REQUIRED
dnl for libgstrtp-1.0: gstreamer-rtp-1.0 >= $GST_REQUIRED
dnl for libgstrtsp-1.0: gstreamer-rtsp-1.0 >= $GST_REQUIRED
dnl etc.
PKG_CHECK_MODULES(GST, [
  gstreamer-1.0 >= $GST_REQUIRED
  gstreamer-base-1.0 >= $GST_REQUIRED
  gstreamer-video-1.0 >= $GST_REQUIRED
  gstreamer-plugins-bad-1.0 >= $GST_REQUIRED
  gstreamer-tag-1.0 >= $GST_REQUIRED
  gstreamer-codecparsers-1.0 >= $GST_REQUIRED
  gstreamer-audio-1.0 >= $GST_REQUIRED
], [
  AC_SUBST(GST_CFLAGS)
  AC_SUBST(GST_LIBS)
], [
  AC_MSG_ERROR([
      You need to install or upgrade the GStreamer development
      packages on your system. On debian-based systems these are
      libgstreamer1.0-dev and libgstreamer-plugins-base1.0-dev.
      on RPM-based systems gstreamer1.0-devel, libgstreamer1.0-devel
      or similar. The minimum version required is $GST_REQUIRED.
  ])
])

PKG_CHECK_MODULES(GSTPB, [
  gstreamer-pbutils-1.0 >= $GST_REQUIRED
], [
  AC_SUBST(GSTPB_CFLAGS)
  AC_SUBST(GSTPB_LIBS)
], [
  AC_MSG_ERROR([
      gstreamer-pbutils-1.0 not found
  ])
])

PKG_CHECK_MODULES(CHECK, [
  gstreamer-check-1.0 >= $GST_REQUIRED
], [
  AC_SUBST(CHECK_CFLAGS)
  AC_SUBST(CHECK_LIBS)
], [
  AC_MSG_ERROR([
      gstreamer-check not found
  ])
])

PKG_CHECK_MODULES(NGI, [
  nemo-gstreamer-interfaces-1.0
], [
  AC_SUBST(NGI_CFLAGS)
  AC_SUBST(NGI_LIBS)
], [
  AC_MSG_ERROR([
      nemo-gstreamer-interfaces not found
  ])
])

PKG_CHECK_MODULES(NGM, [
  nemo-gstreamer-meta-1.0
], [
  AC_SUBST(NGM_CFLAGS)
  AC_SUBST(NGM_LIBS)
], [
  AC_MSG_ERROR([
      nemo-gstreamer-meta not found
  ])
])

PKG_CHECK_MODULES(EGL, [
  egl
], [
  AC_SUBST(EGL_CFLAGS)
  AC_SUBST(EGL_LIBS)
], [
  AC_MSG_ERROR([
      egl not found
  ])
])

PKG_CHECK_MODULES(EXIF, [
  libexif
], [
  AC_SUBST(EXIF_CFLAGS)
  AC_SUBST(EXIF_LIBS)
], [
  AC_MSG_ERROR([
      libexif not found
  ])
])

AC_SEARCH_LIBS([android_dlopen], [hybris-common], [ ],
  AC_MSG_ERROR([libhybris not found])
)

dnl Orc
ORC_CHECK([0.4.17])

dnl check if compiler understands -Wall (if yes, add -Wall to GST_CFLAGS)
AC_MSG_CHECKING([to see if compiler understands -Wall])
save_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([ ], [ ])], [
  GST_CFLAGS="$GST_CFLAGS -Wall"
  AC_MSG_RESULT([yes])
], [
  AC_MSG_RESULT([no])
])

dnl set the plugindir where plugins should be installed (for src/Makefile.am)
if test "x${prefix}" = "x$HOME"; then
  plugindir="$HOME/.gstreamer-1.0/plugins"
else
  plugindir="\$(libdir)/gstreamer-1.0"
fi
AC_SUBST(plugindir)

dnl set proper LDFLAGS for plugins
GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*'
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl our libraries and install dirs use GST_API_VERSION in the filename
dnl to allow side-by-side installation of different API versions
GST_API_VERSION=1.0
AC_SUBST(GST_API_VERSION)
AC_DEFINE_UNQUOTED(GST_API_VERSION, "$GST_API_VERSION",
   [GStreamer API Version])

AC_CONFIG_FILES([Makefile
		common/Makefile
		common/m4/Makefile
		gst-libs/Makefile
		gst-libs/gst/Makefile
		gst-libs/gst/droid/Makefile
		gst/Makefile
		gst/droideglsink/Makefile
		gst/droidcodec/Makefile
		gst/droidcamsrc/Makefile
		tools/Makefile])
AC_OUTPUT

