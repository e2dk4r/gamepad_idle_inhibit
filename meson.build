project(
  'gamepad_idle_inhibit',
  'c',
  license: 'MIT',
  version: '0.1.2',
  default_options: 'c_std=c99'
)

add_project_arguments([
  '-funroll-loops',
  '-fomit-frame-pointer',
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
