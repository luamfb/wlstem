sway_sources = files(
    'config.c',
    'decoration.c',
    'main.c',
    'server.c',
    'xdg_decoration.c',

    'desktop/layer_shell.c',
    'desktop/render.c',
    'desktop/xdg_shell.c',

    'input/input-manager.c',
    'input/cursor.c',
    'input/keyboard.c',
    'input/libinput.c',
    'input/seat.c',
    'input/seatop_default.c',
    'input/seatop_down.c',
    'input/switch.c',
    'input/tablet.c',

    'config/seat.c',
    'config/input.c',

    'bindkey.c',
    'container.c',
    'output.c',
    'view.c',

    'arrange.c',
    'wm.c',
    'window_title.c',
)

sway_deps = [
    cairo,
    libevdev,
    libinput,
    math,
    pango,
    pangocairo,
    pcre,
    glesv2,
    pixman,
    server_protos,
    wayland_server,
    wlroots,
    xkbcommon,
]

if have_xwayland
    sway_sources += 'desktop/xwayland.c'
    sway_deps += xcb
endif

executable(
    'sway',
    sway_sources,
    include_directories: [sway_inc],
    dependencies: sway_deps,
    link_with: [lib_wlstem],
    install: true
)
