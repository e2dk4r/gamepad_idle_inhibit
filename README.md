# gamepad_idle_inhibit

Prevent idling for 30 seconds on any gamepad events on wayland.

For this to work your wayland compositor must support "idle-inhibit-unstable-v1".

Forked from https://github.com/e2dk4r/gamepad and adapted to wayland.

# limits

- Simultaneously 10 gamepad connections detected. So if you were able to connect 11 gamepads at same time this would behave unexpectedly.
- Can read events from maximum of 250 gamepads.

# requirements

- 23 + 9 kilobytes of memory
- Wayland compositor that supports [idle-inhibit-unstable-v1](https://wayland.app/protocols/idle-inhibit-unstable-v1#compositor-support)

# libraries

| library        | used for                            |
|----------------|-------------------------------------|
| liburing       | event loop and polling              |
| libevdev       | detecting whether device is gamepad |
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
