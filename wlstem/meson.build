wlstem_deps = [
    cairo,
    pango,
    pangocairo,
    libevdev,
    libinput,
    math,
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
        'cairo.c',
        'loop.c',
        'pango.c',
        'stringop.c',
        'util.c',
        'output.c',
        'output_config.c',
        'output_manager.c',
        'node.c',
        'server.c',
        'wlstem.c',
    ),
    dependencies: wlstem_deps,
    include_directories: include_directories('include', '../include')
)
