tool_deps = [ gst_dep, gstpbutils_dep ]

executable('dump-resolutions',
  ['common.c', 'resolutions.c'],
  install: true,
  c_args : gstdroid_args,
  include_directories : [configinc, libsinc, droidmedia_h],
  dependencies : tool_deps,
)

executable('dump-camera-parameters',
  ['common.c', 'gstdroidcamparams.c'],
  install: true,
  c_args : gstdroid_args,
  include_directories : [configinc, libsinc, droidmedia_h],
  dependencies : tool_deps,
)
