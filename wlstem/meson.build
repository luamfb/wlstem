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

        'at.c',
        'cursor.c',
        'damage.c',
        'input_method.c',
        'foreach.c',
        'text_input.c',
        'tablet.c',
        'util.c',
        'idle_inhibit_v1.c',
        'misc_protocols.c',
        'output.c',
        'output_config.c',
        'output_handlers.c',
        'output_manager.c',
        'node.c',
        'view.c',
        'render.c',
        'seat.c',
        'server.c',
        'surface.c',
        'transaction.c',
        'window.c',
        'wlstem.c',
    ),
    dependencies: wlstem_deps,
    include_directories: include_directories('include', '../include')
)
