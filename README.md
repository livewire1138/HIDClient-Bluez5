# hidclient — generic Bluetooth HID from Linux

Share selected Linux keyboards and mice with a Bluetooth Classic HID host.
The receiver sees a standard keyboard, mouse and/or media-key device. There are
no PS4-specific addresses, key substitutions, LED paths or pairing commands.

The sender uses Linux evdev and BlueZ 5. CachyOS, Arch and other Linux
distributions can build it; Windows, Linux, Android and Apple devices are intended
receivers when they support these HID functions. **Real-device interoperability
has not yet been validated for this modernization.** This is Classic HID, not
Bluetooth Low Energy HOGP. A Bluetooth adapter must support BR/EDR.

## Build on CachyOS / Arch

```sh
sudo pacman -S --needed base-devel pkgconf bluez bluez-libs bluez-utils glib2 python
make
make check
# Optional installation of the standalone binary:
sudo make install PREFIX=/usr
```

On other Linux distributions, install a C compiler, make, pkg-config, BlueZ
library headers and GIO/GLib development headers. Python 3 is needed only for the
optional launcher and tests. The executable has no Python or X11 dependency.
`make install` respects `PREFIX` (default `/usr/local`) and `DESTDIR`.

## Configure BlueZ once

Registration uses the supported D-Bus ProfileManager API; `--compat`, `sdptool`,
`hciconfig` and the deprecated BlueZ tools are unnecessary. The program owns both
Classic HID L2CAP channels and registers only its SDP service record with BlueZ.

BlueZ's built-in **input** plugin owns the same profile and ports. Inspect
`systemctl cat bluetooth.service`, then create a drop-in with
`sudo systemctl edit bluetooth.service`:

```ini
[Service]
ExecStart=
ExecStart=/usr/lib/bluetooth/bluetoothd --noplugin=input
```

Use the executable path from your installed unit and preserve its other options.
Remove the old `--compat` option if it was added solely for hidclient. Apply with
`sudo systemctl daemon-reload` and `sudo systemctl restart bluetooth.service`.
The restart disconnects existing Bluetooth sessions. Disabling input affects
Bluetooth keyboards/mice/controllers received by this Linux machine, across all
its adapters. A second adapter alone does not isolate this daemon-wide setting.
To undo this setup, remove only the drop-in you created and reload/restart BlueZ.

`--adapter hciN` selects the transport adapter, but BlueZ's ProfileManager
publishes the SDP record across its powered adapters. Use one powered adapter
for initial testing; other adapters may advertise a service they cannot accept.
The launcher does not change system settings or restart Bluetooth.

## Select inputs and run

```sh
sudo ./hidclient -l
sudo ./hidclient --adapter hci0 --mode combo \
  --device /dev/input/by-id/YOUR-KEYBOARD-event-kbd \
  --device /dev/input/by-id/YOUR-MOUSE-event-mouse --grab
```

Replace the paths with your actual inputs. `/dev/input/by-path/` also works.
Repeat `--device` as needed; select each underlying event node only once.
`--mode keyboard`, `mouse` or `combo` controls which HID collections are
advertised; keyboard mode includes media keys. Use keyboard mode for a
keyboard-only receiver. Arbitrary gamepad, touchscreen, tablet and proprietary
vendor reports are not translated.

`--grab` captures the selected devices exclusively **while connected**, on
Wayland, X11 or a console. On disconnect or exit, they return to the local
machine. Without `--grab`, events also reach the local desktop. Root is the
straightforward initial setup because evdev access and Bluetooth sockets require
appropriate permissions.

Optional settings:

- `--host AA:BB:CC:DD:EE:FF`: accept channels only from this receiver.
- `--name "My keyboard"`: SDP service name; the discovery name is the adapter's
  alias, configured separately in bluetoothctl.
- `--swap-ctrl-meta`: exchange Control with Command/Windows on both sides.
- `--exit-key F12`: Ctrl+Alt+F12 exits by default. `none` disables the chord.
- `--disconnect-key ScrollLock`: optional Ctrl+Alt+ScrollLock disconnection.
  It is disabled by default. Plain Pause and Scroll Lock are normal keys.
- Ctrl+C, SIGTERM and SIGHUP stop the process and release its inputs.

Legacy `-eNUM`, `-l` and `-x` remain available. No selection is an error;
`--all` explicitly selects every event input, up to 64. The Python wrapper is
optional and replaces itself with the binary:

```sh
sudo python3 launch.py --device /dev/input/by-id/YOUR-KEYBOARD-event-kbd \
  --mode keyboard --target AA:BB:CC:DD:EE:FF
```

The wrapper grabs by default (`--no-grab` disables it). `--target` restricts the
receiver; initiate the connection on that receiver. There is no shell-spawned
connection loop or daemon restart.

## Pair and reconnect

Keep an interactive `bluetoothctl` terminal open during pairing. In it, use
`list` and `select ADAPTER-MAC` to choose the same adapter as `--adapter`, then:

```text
power on
system-alias Shared keyboard
agent KeyboardDisplay
default-agent
pairable on
discoverable on
```

Start hidclient in another terminal, then pair from the receiving device's
Bluetooth settings. Follow the prompts shown by bluetoothctl and the receiver;
the agent terminal handles authentication. Keep a local input device or terminal
available during setup. Both sockets request authenticated, encrypted
connections. Do not downgrade pairing security to troubleshoot Apple devices.
After pairing, turn discoverability off. Reconnect using the receiver's settings.
After changing report mode or upgrading from the old descriptor, remove the old
pairing on both devices and pair again if the receiver caches its old capabilities.

## Apple receivers

The standard modifier mapping is Meta/Windows → **Command**, Alt → **Option**,
and Control → **Control**. `--swap-ctrl-meta` is optional for users who prefer
Control in Command's position. Choose the matching physical keyboard layout on
the receiver; the program forwards key positions, not composed text or the Linux
input method. Fn/Globe, Touch ID and Apple-specific gestures are not emulated.

- **Mac:** pair in System Settings → Bluetooth. Keyboard, ordinary mouse motion,
  buttons and scrolling use generic HID reports. Start with keyboard mode if
  diagnosing pairing, then re-pair in combo mode to add a mouse.
- **iPad:** pair the keyboard or combo device in Settings → Bluetooth. iPadOS
  supports generic Bluetooth pointing devices. Configure hardware keyboard layout
  and modifier preferences in the iPad's settings as needed.
- **iPhone:** a hardware keyboard uses HID. For mouse input, enable Settings →
  Accessibility → Touch → AssistiveTouch, then use Devices → Bluetooth Devices
  to pair the pointer. Pointer support follows AssistiveTouch's capabilities.

Apple documents HID support and requires encrypted Bluetooth pairing on iOS and
iPadOS. This implementation requests encryption and provides standard report and
boot protocols, keyboard LED output handling, and media-key reports. Those are
compatibility measures, **not a claim of Apple certification or successful device
testing**. LED output is tracked for protocol replies; it is not written back to
physical keyboard LEDs. Media and extra mouse-button behavior depends on the host.

References: [Apple Bluetooth profiles](https://support.apple.com/en-us/102842),
[Apple pointer setup](https://support.apple.com/en-us/111775),
[iPad mouse support](https://support.apple.com/en-lamr/guide/ipad/ipad10939edf/ipados).

## Implementation and verification

Report encoding (`hid.c`) is separate from Linux input/socket handling
(`hidclient.c`) and D-Bus registration (`bluez.c`). Reports include six-key
rollover plus modifiers, international keyboard usages, media keys, five mouse
buttons, vertical/horizontal wheel and frame-grouped relative motion. Large
movements are split into valid packets; high-resolution wheel events accumulate
without double-counting legacy wheel events.

Input overruns recover from an evdev key-state snapshot. Connection setup also
snapshots held keys. Either HID channel may arrive first, both must have the same
peer, and an incomplete channel pair times out. Suspend/resume, protocol requests
and output reports are handled. Virtual-cable unplug closes interrupt before control
and removes only that receiver's bond through the selected BlueZ adapter. If bond
removal fails, hidclient stops rather than accepting a stale paired connection. Signals, device removal, daemon loss and peer
closure have explicit cleanup. A removed input stops the program; restart it
after reconnecting the device. Restart hidclient after restarting BlueZ.

`make check` runs report/protocol tests, descriptor and XML checks, twelve
socket/FIFO lifecycle scenarios, CLI tests, and real D-Bus messages against a
local fake service. It does not touch the system daemon or radio. USB keyboards,
real evdev grabs, Bluetooth pairing/encryption, sleep/reconnect and each receiver
OS still need hardware testing. An optional FIFO accepts native Linux
`input_event` records (`-fPATH`); it cannot recover an overrun because it lacks
an evdev snapshot. `--print-sdp` and `--print-descriptor` work offline;
`-s` skips registration for transport debugging only.

Protocol references: [USB HID 1.11](https://www.usb.org/sites/default/files/documents/hid1_11.pdf),
[BlueZ ProfileManager](https://github.com/bluez/bluez/blob/master/doc/org.bluez.ProfileManager.rst),
[Linux input events](https://docs.kernel.org/input/event-codes.html).
The original source declares GPL-2.0-only licensing.
