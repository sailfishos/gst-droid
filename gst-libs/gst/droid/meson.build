gstdroid_sources = [
  '/usr/share/droidmedia/hybris.c',
  'gstdroidbufferpool.c',
  'gstdroidcodec.c',
  'gstdroidmediabuffer.c',
  'gstdroidquery.c',
  'gstwrappedmemory.c',
]

gstdroid_headers = [
  'gstdroidbufferpool.h',
  'gstdroidcodec.h',
  'gstdroidmediabuffer.h',
  'gstdroidquery.h',
  'gstwrappedmemory.h',
]

gstdroid_deps = [
  gst_dep,
  gstbase_dep,
  gstcodecparsers_dep,
  gstvideo_dep,
  egl_dep,
  gstnemointerfaces_dep,
  hybris_dep
]

install_headers(gstdroid_headers, subdir : 'gstreamer-@0@/gst/allocators'.format(api_version))

gstdroid = library('gstdroid-@0@'.format(api_version),
  gstdroid_sources + gstdroid_headers,
  c_args : gstdroid_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc, droidmedia_h],
  version : lib_version,
  soversion : so_version,
  install : true,
  dependencies : gstdroid_deps
)

gstdroid_dep = declare_dependency(link_with: gstdroid,
  include_directories : [libsinc],
  dependencies : gstdroid_deps)
