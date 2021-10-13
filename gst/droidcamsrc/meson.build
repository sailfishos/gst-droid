gstdroidcamsrc_sources = [
  '/usr/share/droidmedia/hybris.c',
  'gstdroidcamsrc.c',
  'gstdroidcamsrcdev.c',
  'gstdroidcamsrcparams.c',
  'gstdroidcamsrcenums.c',
  'gstdroidcamsrcphotography.c',
  'gstdroidcamsrcquirks.c',
  'gstdroidcamsrcexif.c',
  'gstdroidcamsrcmode.c',
  'gstdroidcamsrcrecorder.c'
]

gstdroidcamsrc_headers = [
  'gstdroidcamsrc.h',
  'gstdroidcamsrcdev.h',
  'gstdroidcamsrcparams.h',
  'gstdroidcamsrcenums.h',
  'gstdroidcamsrcphotography.h',
  'gstdroidcamsrcquirks.h',
  'gstdroidcamsrcexif.h',
  'gstdroidcamsrcmode.h',
  'gstdroidcamsrcrecorder.h'
]

gstdroidcamsrc_deps = [
  gst_dep,
  gstbase_dep,
  gstcamerabinsrc_dep,
  gstdroid_dep,
  gstnemometa_dep,
  gstphotography_dep,
  gsttag_dep,
  gstvideo_dep,
  egl_dep,
  exif_dep,
  hybris_dep
]

gstdroidcamsrc = static_library('gstdroidcamsrc-@0@'.format(api_version),
  gstdroidcamsrc_sources + gstdroidcamsrc_headers,
  c_args : gstdroid_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : ['..', configinc, libsinc, droidmedia_h],
  dependencies : gstdroidcamsrc_deps
)

gstdroidcamsrc_dep = declare_dependency(link_with: gstdroidcamsrc,
  include_directories : [libsinc],
  dependencies : gstdroidcamsrc_deps)
