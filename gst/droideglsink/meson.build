gstdroideglsink_sources = [
  '/usr/share/droidmedia/hybris.c',
  'gstdroideglsink.c',
  'gstdroidvideotexturesink.c'
]

gstdroideglsink_headers = [
  'gstdroideglsink.h',
  'gstdroidvideotexturesink.h'
]

gstdroideglsink_deps = [
  gst_dep,
  gstdroid_dep,
  gstnemointerfaces_dep,
  gstvideo_dep,
  egl_dep
]

gstdroideglsink = static_library('gstdroideglsink-@0@'.format(api_version),
  gstdroideglsink_sources + gstdroideglsink_headers,
  c_args : gstdroid_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : ['..', configinc, libsinc, droidmedia_h],
  dependencies : gstdroideglsink_deps
)

gstdroideglsink_dep = declare_dependency(link_with: gstdroideglsink,
  include_directories : [libsinc],
  dependencies : gstdroideglsink_deps)
