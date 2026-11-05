gstdroidscreencapsrc_sources = [
  'gstdroidscreencapsrc.c'
]

gstdroidscreencapsrc_headers = [
  'gstdroidscreencapsrc.h'
]

gstdroidscreencapsrc_deps = [
  droidmedia_dep,
  gst_dep,
  gstbase_dep
]

gstdroidscreencapsrc = static_library('gstdroidscreencapsrc-@0@'.format(api_version),
  gstdroidscreencapsrc_sources + gstdroidscreencapsrc_headers,
  c_args : gstdroid_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : ['..', configinc, libsinc],
  dependencies : gstdroidscreencapsrc_deps
)

gstdroidscreencapsrc_dep = declare_dependency(link_with: gstdroidscreencapsrc,
  include_directories : [libsinc],
  dependencies : gstdroidscreencapsrc_deps)
