# The tests define the droidmedia calls they need themselves, in place of
# the libhybris glue in libgstdroid, so they run without Android.
droid_tests = [
  'libs/droid',
]

foreach t : droid_tests
  name = t.underscorify()
  exe = executable(name, '@0@.c'.format(t),
    c_args : gstdroid_args + ['-DGST_USE_UNSTABLE_API'],
    include_directories : [configinc, libsinc],
    dependencies : [gstdroid_dep, gstcheck_dep],
    install : false)
  test(name, exe,
    env : ['GST_REGISTRY=@0@/@1@.registry'.format(meson.current_build_dir(), name),
           'GST_PLUGIN_SYSTEM_PATH_1_0=', 'GST_PLUGIN_PATH_1_0=',
           'CK_DEFAULT_TIMEOUT=60'],
    timeout : 120)
endforeach
