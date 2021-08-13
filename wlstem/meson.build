wlstem_deps = [
    cairo,
    libevdev,
    libinput,
    math,
    pango,
    pcre,
    glesv2,
    pixman,
    server_protos,
    wayland_server,
    wlroots,
    xkbcommon,
]

if have_xwayland
    wlstem_deps += xcb
endif

lib_wlstem = shared_library(
    'wlstem',
    files(
        'exec.c',
        'list.c',
        'log.c',
    ),
    dependencies: wlstem_deps,
    include_directories: include_directories('include')
)
