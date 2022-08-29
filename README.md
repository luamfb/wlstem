# DEPRECATION NOTE
This software is now deprecated. Use something else.

# TL;DR

Any serious (non-toy) Wayland compositor using wlroots would have to
reimplement half of sway's code.
This library hopes to provide that half, while keeping its nose out of
WM decisions.

# wlstem

**wlstem** is a WIP high-level library that aids in creating Wayland
compositors, built on top of [wlroots].

[wlroots] does a lot of the heavy work for those who wish to create
Wayland compositors -- but not all of it.
This library's main goal is to provide functionality that virtually any
compositor using wlroots will want to implement, and which does not vary
with the compositor's window management policies.

**wlstem** started as a fork of [sway], the i3-compatible
Wayland compositor made by wlroots' creator.
The remaining code from sway, currently located at `mod_sway` directory,
now serves as an example Wayland compositor using this library.

_Note_: this library is work in progress: features are still being
transferred from sway's modified code to wlstem.

## Dependencies

- [wlroots]
- meson (\*)
- pcre
- json-c
- pango
- cairo
- pixman
- Wayland's core libraries:
  - wayland-server
  - wayland-client
  - wayland-cursor
  - wayland-egl
- wayland-protocols (\*)
- evdev
- libinput
- xkbcommon (†)
- mesa (GLESv2) (‡)

(\*) Compile-time dependencies.  
(†) Only if xwayland support is desired.  
(‡) Usually installed by default at Unix-like systems

## Building

Ensure all dependencies are installed, then build with

```
meson build/
ninja -C build/
```

And optionally install with

```
sudo ninja -C build/ install
```

The resulting library shall be located at `build/wlstem/libwlstem.so`
and the example program, at `build/mod_sway/sway`.

[wlroots]: https://github.com/swaywm/wlroots
[sway]: https://github.com/swaywm/sway

## Running the example program

Run the generated `sway` binary from a TTY, also known as a
[console](https://en.wikipedia.org/wiki/Linux_console).
