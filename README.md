# gamepad_idle_inhibit

After
[cc482228a41e9e3e31b6b27143d5ed24e7dc5069](https://github.com/swaywm/sway/commit/cc482228a41e9e3e31b6b27143d5ed24e7dc5069)
commit, it no longer works on sway (>=v1.11-rc1). You can revert it manually then compile.

Prevent idling for 30 seconds on any gamepad events on wayland.

For this to work your wayland compositor must support "idle-inhibit-unstable-v1".

Forked from https://github.com/e2dk4r/gamepad and adapted to wayland.

# limits

- Simultaneously 10 gamepad connections detected. So if you were able to connect 11 gamepads at same time this would behave unexpectedly.
- Can read events from maximum of 250 gamepads.

# requirements

- Whopping 24 + 1 kilobytes of memory
- Wayland compositor that supports [idle-inhibit-unstable-v1](https://wayland.app/protocols/idle-inhibit-unstable-v1#compositor-support)

# libraries

| library        | used for                            |
|----------------|-------------------------------------|
| liburing       | event loop and polling              |
| wayland-client | connecting to wayland display       |

# build

```
meson setup build
ninja -C build
./build/gamepad
```

# references

- see chapter "5. Event interface" in https://www.kernel.org/doc/Documentation/input/input.txt
- see https://www.kernel.org/doc/Documentation/input/gamepad.txt
- see the key codes included in `/usr/include/linux/input-event-codes.h`
- see https://unixism.net/loti/tutorial/index.html for liburing examples
- if you have libinput on your system, see `man libinput-record(1)` for debugging input events
- see https://wayland-book.com/ for introduction to wayland
