gstdroidcodec_sources = [
  'gstdroidvdec.c',
  'gstdroidvenc.c',
  'gstdroidadec.c',
  'gstdroidaenc.c'
]

gstdroidcodec_headers = [
  'gstdroidvdec.h',
  'gstdroidvenc.h',
  'gstdroidadec.h',
  'gstdroidaenc.h'
]

gstdroidcodec_deps = [
  droidmedia_dep,
  gst_dep,
  gstaudio_dep,
  gstbase_dep,
  gstcodecparsers_dep,
  gstdroid_dep,
  gstvideo_dep,
  egl_dep,
  orc_dep
]

gstdroidcodec = static_library('gstdroidcodec-@0@'.format(api_version),
  gstdroidcodec_sources + gstdroidcodec_headers,
  c_args : gstdroid_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : ['..', configinc, libsinc],
  dependencies : gstdroidcodec_deps
)

gstdroidcodec_dep = declare_dependency(link_with: gstdroidcodec,
  include_directories : [libsinc],
  dependencies : gstdroidcodec_deps)
