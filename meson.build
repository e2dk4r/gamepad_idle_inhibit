project(
  'gamepad_idle_inhibit',
  'c',
  license: 'MIT',
  version: '0.1.2',
  default_options: 'c_std=c99'
)

is_build_debug = get_option('buildtype').startswith('debug')
cc = meson.get_compiler('c')

add_project_arguments(
  [
    '-funroll-loops',
    '-fomit-frame-pointer',

    '-DCOMPILER_GCC=' + (cc.get_id() == 'gcc').to_int().to_string(),
    '-DCOMPILER_CLANG=' + (cc.get_id() == 'clang').to_int().to_string(),
    '-DGAMEPAD_IDLE_INHIBIT_DEBUG=' + is_build_debug.to_int().to_string(),
  ],
  language: 'c'
)

libevdev = dependency('libevdev')
liburing = dependency('liburing')
wayland_client = dependency('wayland-client')
wayland_protocols = dependency('wayland-protocols', native: true)

subdir('protocol')

sources = files([
  'src/main.c'
])

executable(
  'gamepad_idle_inhibit',
  sources: sources + wl_protocols_src,
  dependencies: [
    libevdev,
    liburing,
    wayland_client,
  ],
  install: true,
)
