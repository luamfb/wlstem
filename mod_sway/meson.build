sway_sources = files(
    'config.c',
    'decoration.c',
    'main.c',
    'server.c',
    'xdg_decoration.c',

    'desktop/desktop.c',
    'desktop/idle_inhibit_v1.c',
    'desktop/layer_shell.c',
    'desktop/output.c',
    'desktop/render.c',
    'desktop/surface.c',
    'desktop/transaction.c',
    'desktop/xdg_shell.c',

    'input/input-manager.c',
    'input/cursor.c',
    'input/keyboard.c',
    'input/libinput.c',
    'input/seat.c',
    'input/seatop_default.c',
    'input/seatop_down.c',
    'input/seatop_resize_tiling.c',
    'input/switch.c',
    'input/tablet.c',
    'input/text_input.c',

    'config/output.c',
    'config/seat.c',
    'config/input.c',

    'bindkey.c',
    'exec.c',

    'tree/arrange.c',
    'tree/container.c',
    'tree/node.c',
    'tree/root.c',
    'tree/view.c',
    'tree/workspace.c',
    'tree/output.c',
)

sway_deps = [
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
    sway_sources += 'desktop/xwayland.c'
    sway_deps += xcb
endif

executable(
    'sway',
    sway_sources,
    include_directories: [sway_inc],
    dependencies: sway_deps,
    link_with: [lib_sway_common],
    install: true
)
