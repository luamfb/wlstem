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
        'user_callbacks.c',
        'misc_protocols.c',
        'server.c',
        'window.c',
        'wlstem.c',

        'input/cursor.c',
        'input/idle_inhibit_v1.c',
        'input/input_method.c',
        'input/text_input.c',
        'input/tablet.c',
        'input/seat.c',

        'output/output.c',
        'output/output_config.c',
        'output/output_handlers.c',
        'output/output_manager.c',

        'render/damage.c',
        'render/render.c',
        'render/surface.c',
        'render/view.c',

        'transaction/node.c',
        'transaction/transaction.c',

        'util/at.c',
        'util/exec.c',
        'util/foreach.c',
        'util/list.c',
        'util/log.c',

        'old/cairo.c',
        'old/loop.c',
        'old/pango.c',
        'old/stringop.c',
        'old/util.c',
    ),
    dependencies: wlstem_deps,
    include_directories: include_directories('include', '../include')
)
