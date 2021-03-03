tool_deps = [ gst_dep, gstpbutils_dep ]

executable('record-video',
  ['common.c', 'record.c'],
  install: true,
  c_args : gstdroid_args,
  include_directories : [configinc, libsinc, droidmedia_h],
  dependencies : tool_deps,
)

executable('dump-resolutions',
  ['common.c', 'resolutions.c'],
  install: true,
  c_args : gstdroid_args,
  include_directories : [configinc, libsinc, droidmedia_h],
  dependencies : tool_deps,
)

executable('mk-cam-conf',
  ['common.c', 'record.c'],
  install: true,
  c_args : gstdroid_args,
  include_directories : [configinc, libsinc, droidmedia_h],
  dependencies : tool_deps,
)

