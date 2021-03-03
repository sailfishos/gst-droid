project('gst-droid', 'c', 'cpp',
  version : '1.0.0',
  default_options : [ 'warning_level=1',
                      'buildtype=debugoptimized' ])

api_version = '1.0'
lib_version = '1.0.0'
so_version = '0'

gstdroid_version = meson.project_version()
gstdroid_name = meson.project_name()

cc = meson.get_compiler('c')

droid_conf = configuration_data()

gst_req = '>= 1.0.0'

# Mandatory GST deps
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'audio_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'pbutils_dep'])
gsttag_dep = dependency('gstreamer-tag-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'tag_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'video_dep'])
gstpluginsbad_dep = dependency('gstreamer-plugins-bad-1.0', version : gst_req,
  fallback : ['gst-plugins-bad', 'plugins_bad_dep'])
gstcodecparsers_dep = dependency('gstreamer-codecparsers-1.0', version : gst_req,
  fallback : ['gstreamer-plugins-bad', 'codecparsers_dep'])
gstphotography_dep = dependency('gstreamer-photography-1.0', version : gst_req,
  fallback : ['gstreamer-plugins-bad', 'photography_dep'])

gstcamerabinsrc_dep = cc.find_library('gstbasecamerabinsrc-1.0', required: true)
gstnemointerfaces_dep = dependency('nemo-gstreamer-interfaces-1.0', required : true)
gstnemometa_dep = dependency('nemo-gstreamer-meta-1.0', required : true)

egl_dep = dependency('egl', required : true)
exif_dep = dependency('libexif', required : true)
orc_dep = dependency('orc-0.4', required : true)

if orc_dep.found()
  droid_conf.set('HAVE_ORC', 1)
endif

droid_conf.set_quoted('PACKAGE', 'gstreamer-droid')
droid_conf.set_quoted('VERSION', '@0@'.format(lib_version))
droid_conf.set_quoted('PACKAGE_NAME', 'GstDroid library')
droid_conf.set_quoted('PACKAGE_STRING', 'GstDroid library')

root_dir = include_directories('.')
droidmedia_h = include_directories('/usr/include/droidmedia')
hybris_dep = cc.find_library('hybris-common', required: true)

gstdroid_args = ['-DHAVE_CONFIG_H', '-DSYSCONFDIR="/etc"']

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

configinc = include_directories('.')
libsinc = include_directories('gst-libs')

subdir('gst-libs')
subdir('gst')
subdir('tools')

pkg = import('pkgconfig')

pkgconf = configuration_data()
pkgconf.set('prefix', join_paths(get_option('prefix')))
pkgconf.set('exec_prefix', '${prefix}')
pkgconf.set('libdir', '${prefix}/@0@'.format(get_option('libdir')))
pkgconf.set('includedir', '${prefix}/@0@'.format(get_option('includedir')))
pkgconf.set('VERSION', lib_version)

pkg_install_dir = '@0@/pkgconfig'.format(get_option('libdir'))

infile = 'gstreamer-droid-@0@.pc.in'.format(api_version)
outfile = 'gstreamer-droid-@0@.pc'.format(api_version)
configure_file(input : infile,
  output : outfile,
  configuration : pkgconf,
  install_dir : pkg_install_dir)

configure_file(output: 'config.h', configuration: droid_conf)
