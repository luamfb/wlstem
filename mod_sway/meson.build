sway_sources = files(
    'commands.c',
    'config.c',
    'criteria.c',
    'decoration.c',
    'ipc-json.c',
    'ipc-server.c',
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
    'input/seatop_move_floating.c',
    'input/seatop_move_tiling.c',
    'input/seatop_resize_floating.c',
    'input/seatop_resize_tiling.c',
    'input/switch.c',
    'input/tablet.c',
    'input/text_input.c',

    'config/output.c',
    'config/seat.c',
    'config/input.c',

    'commands/bind.c',
    'commands/exec.c',
    'commands/exec_always.c',
    'commands/focus.c',
    'commands/for_window.c',
    'commands/kill.c',
    'commands/input.c',
    'commands/layout.c',
    'commands/output.c',
    'commands/resize.c',
    'commands/seat.c',
    'commands/seat/attach.c',
    'commands/seat/cursor.c',
    'commands/seat/idle.c',
    'commands/set.c',
    'commands/swap.c',
    'commands/workspace.c',

    'commands/input/events.c',
    'commands/input/map_from_region.c',
    'commands/input/map_to_region.c',
    'commands/input/tool_mode.c',
    'commands/input/xkb_file.c',
    'commands/input/xkb_switch_layout.c',

    'commands/output/background.c',

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
    jsonc,
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
