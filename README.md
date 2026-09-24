# hidpoll

Runtime HID / USB polling-rate patcher for the Nintendo Switch (Atmosphere sysmodule).

Raises the rate at which `hid` samples controllers and the rate at which `usb`
polls full-speed interrupt endpoints, so games actually see the higher-poll
data from 500/1000 Hz controllers instead of being capped at the stock 200 Hz.

---

## Install

1. Grab the latest release zip.
2. Extract it to the **root of your SD card** (merge with existing folders).
3. Reboot. The sysmodule auto-starts and applies the default
   settings on every boot.

### Verify Installation

```
sd:/
├── atmosphere/
│   └── contents/
│       └── 420000000048504C/
│           ├── exefs.nsp
│           └── flags/
│               └── boot2.flag
├── config/
│   └── hidpoll/
│       ├── config.ini
│       └── log.txt
└── switch/
    ├── hidpoll_ctl.nro
    └── .overlays/
        └── hidpoll_ovl.ovl
```

## Config

Edit `sd:/config/hidpoll/config.ini`. Changes take effect on the next boot,
or immediately if applied via `hidpoll_ctl.nro`.

```ini
hid_hz=1000
usb_hz=1000
```

| key       | allowed values             | notes                                                       |
|-----------|----------------------------|-------------------------------------------------------------|
| `hid_hz`  | `125`, `200`, `250`, `500`, `1000` | Switch default is 200                                       |
| `usb_hz`  | `0`, `125`, `250`, `500`, `1000`   | `0` = honor the device's `bInterval` (Switch default)       |

## Front-ends

Two optional front-ends ship with the same feature set, pick whichever fits
your workflow:

- **hidpoll_ctl.nro**: full-screen SDL2 homebrew app, launched from the hbmenu.
- **hidpoll_ovl.ovl**: [Ultrahand](https://github.com/ppkantorski/Ultrahand-Overlay)
  overlay (libultrahand). Open the Tesla menu (`L + \u2295 + \u2190` by
  default), pick **hidpoll**. Runs on top of any game.

Both talk to the sysmodule via the `hidpoll` IPC service and expose:
Settings (HID/USB rate + apply/reapply), Poll Rate Test, and Status.

## hidpoll_ctl (SDL2 homebrew app)

Launch from the hbmenu (**Album → hidpoll control**). Three tabs:

- **Settings**: pick a rate with the D-pad, press `A` to apply one row or
  `Y` to apply both. `X` re-runs the patcher.
- **Poll Rate Test**: see below.
- **Status**: shows what the sysmodule managed to patch, and any errors.

Bottom bar always shows the current button hints. `+` exits.

## hidpoll_ovl (Ultrahand overlay)

Requires [Ultrahand Overlay](https://github.com/ppkantorski/Ultrahand-Overlay)
(or plain [nx-ovlloader](https://github.com/WerWolv/nx-ovlloader)). Same
sections presented as pages:

- **Main**: HID + USB rate track-bars. `\uE0E0 A` applies the highlighted
  bar; on the HID bar, `\uE0E3 Y` applies both HID and USB. **Reapply
  patcher** re-runs the pattern scan; **Poll Rate Test** / **Status** push
  sub-pages.
- **Poll Rate Test**: `\uE0E0 A` on **Start / Stop** toggles the sampler
  (or press `\uE0E1 B` to leave the page, which also stops it). Spin both
  sticks while it runs; live SAMPLER / STICKS / missed counters update
  every frame.
- **Status**: same flag list as hidpoll_ctl's Status tab.

Rate changes trigger a toast notification with the result / rc.

### Poll Rate Test

Measures the real polling rate reaching games:

1. Open the **Poll Rate Test** tab, press `A`.
2. **Spin both analog sticks continuously and quickly.**
3. Press `B` to stop.

Reported numbers:

- **SAMPLER**: new samples per second from hid's Npad LIFO. This is the raw
  sampling rate; it ticks even without input. Expect ≈ `hid_hz`.
- **STICK RATE**: samples per second whose L/R stick values actually differ
  from the previous sample. This is the real controller update rate a game
  sees. Spin fast enough that every sample changes.
- **missed**: LIFO overruns. Should stay at 0.

Typical results:

| setup                                                  | sampler | stick rate |
|--------------------------------------------------------|--------:|-----------:|
| stock (`hid_hz=200`, `usb_hz=0`, 8 ms bInterval pad)   |     200 |        125 |
| hid patched only (`hid_hz=1000`)                       |    1000 |        125 |
| hid + usb patched, 1 kHz-capable pad, replugged        |    1000 |      ~1000 |

## Tested Controllers

A true 1 kHz report rate needs support from the controller's hardware and
firmware. hidpoll can't make a controller report faster than it already does.
These controllers have been tested so far. Open an issue to add a controller
or correct an entry.

✅ full 1 kHz · 🟡 works, with caveats · ❌ unsupported · ❔ untested

|    | controller                                 | report rate | notes                                    |
|:--:|--------------------------------------------|------------:|------------------------------------------|
| ✅ | GameCube controller, official adapter      |       ~1000 | reaches or comes close to 1 kHz          |
| ✅ | GameCube controller, lossless adapter      |        1000 |                                          |
| ❌ | HOJA ProGCC (Switch mode), stock firmware  |         125 | firmware limits it to 125 Hz             |
| ✅ | HOJA ProGCC (Switch mode), custom firmware |        1000 |                                          |
| ❔ | Nintendo Switch Pro Controller             |           ? | untested; probably unable to reach 1 kHz |

## Notes and caveats

- USB interval is baked into the xHCI endpoint context when the endpoint is
  created. **After changing `usb_hz` you must unplug/replug the controller**
  for the change to take effect.
- The USB patch is global, every full-speed interrupt endpoint (keyboards,
  mice, third-party pads) gets the forced interval. This is within spec and
  most devices behave, but odd devices might not.
- 17 hid sampler threads at 1 kHz each hasn't been stress-tested with many
  simultaneous USB devices; lower `hid_hz` if you see problems.

## Troubleshooting

- Check `sd:/config/hidpoll/log.txt`. It's rewritten on every boot and
  every apply.
- In `hidpoll_ctl`, the **Status** tab shows which pieces the patcher
  attached to, patched, and live-poked.
- If **hid text patched** is off, the pattern didn't match on your firmware
  (see "Adding a new hid build id" below).
- If **hid live tasks poked** is off but text-patched is on, the sysmodule
  doesn't know the offsets for your firmware. Rates will still change,
  just not until the next Npad activation / controller re-attach.

---

# For Developers

## Building

Requires [devkitPro](https://devkitpro.org/) with the `switch-dev` package
group installed. Extra packages:

- `switch-sdl2` + `switch-sdl2_ttf`: hidpoll_ctl (homebrew front-end)
- `switch-curl` + `switch-zlib` + `switch-mbedtls`: hidpoll_ovl
  (Ultrahand overlay; libultrahand is fetched automatically into
  `frontends/overlay/hidpoll_ovl/lib/libultrahand` on first build).
  `libminizip.a` ships inside `switch-zlib`; there is no separate
  `switch-minizip`.

```
DEVKITPRO=/opt/devkitpro bash build.sh          # builds all three, stages to ./out/
DEVKITPRO=/opt/devkitpro bash build.sh clean    # wipe build artifacts
```

`out/` mirrors the SD layout above. Copy it straight to the sd card.

## Repo layout

```
hidpoll/
├── build.sh                              # one-shot builder + stager
├── out/                                  # generated; drop into sd root
├── sysmodule/
│   └── hidpoll/                          # the sysmodule (C)
│       ├── hidpoll.json                  # NPDM: title id + capabilities
│       ├── include/hidpoll.h             # shared public header (IPC + client helpers)
│       └── source/
│           ├── main.c                    # libnx boot glue + entry
│           ├── state.[ch]                # config, g_hid_hz/g_usb_hz, apply_all
│           ├── service.[ch]              # cmif IPC server
│           ├── patcher.h                 # public entry points
│           ├── patcher_hid.c             # hid patterns + live task poke
│           ├── patcher_usb.c             # usb xHCI FS-interrupt patch
│           ├── pattern.[ch]              # instruction pattern scanning
│           ├── target.[ch]               # svcDebugActiveProcess + memory r/w
│           ├── arm64.h                   # A64 encode/decode helpers
│           └── log.[ch]                  # log.txt writer
└── frontends/
    ├── homebrew/
    │   └── hidpoll_ctl/                  # SDL2 GUI front-end (C++)
    │       └── source/
    │           ├── main.cpp              # boot, main loop, input dispatch
    │           ├── theme.hpp             # colors, dimensions
    │           ├── ui.[hpp/cpp]          # SDL/TTF init + draw primitives
    │           ├── state.[hpp/cpp]       # AppState, presets, tabs
    │           ├── tester.[hpp/cpp]      # sampler thread for the Poll Rate Test
    │           └── panels.[hpp/cpp]      # sidebar + per-tab drawing
    └── overlay/
        └── hidpoll_ovl/                  # Ultrahand overlay (libultrahand)
            ├── Makefile
            ├── lib/libultrahand/         # fetched by build.sh (gitignored)
            └── source/
                ├── main.cpp              # pages: Main / Test / Status, IPC glue
                └── tester.[hpp/cpp]      # same LIFO sampler as hidpoll_ctl
```

Both frontends `#include "hidpoll.h"` from `sysmodule/hidpoll/include/`. The
sysmodule owns the IPC contract and each frontend just adds it to its
Makefile's `INCLUDES` list.

## How it works

**Program ID** `420000000048504C`  •  **Service** `hidpoll`

On boot, the sysmodule attaches to the running `hid` and `usb` processes
with `svcDebugActiveProcess`, finds the polling-rate code by instruction
pattern (so it survives minor firmware changes), rewrites the constants in
memory, and pokes hid's live task objects so the new rate applies without
a reboot. Then it registers the `hidpoll` IPC service so overlays / homebrew
can change the rate at runtime.

Why a sysmodule instead of an exefs (IPS) patch: `usb` is launched by
Atmosphere **before** the SD card is mounted (FS needs `usb` to mount the
card on 9.0.0+), so `loader` can never read an `exefs_patches` IPS for it.
`hid` could use an IPS, but doing both at runtime gives a single
live-adjustable mechanism.

### Patch sites

| target | site | effect |
|---|---|---|
| hid | `ResourceManager::Activate` — `SetInterval(npad_task, 5 ms)` constant | Npad shared-memory sampler rate |
| hid | Ahid device attach — `SetInterval(mode1_task, 8 ms)` / `SetInterval(mode2_task, 8 ms)` constants | USB-HID read cadence for devices attached later |
| hid | live poke of `task->period` on the running Npad task and every AhidSampler task | takes effect within one tick, no replug (requires known build id) |
| usb | `XhciDriver::GetEndpointInterval` FS-interrupt branch: `lsl w0,w0,#3` → `movz w0,#(8·2^k)` | forces every full-speed interrupt endpoint to the chosen interval regardless of `bInterval`. `usb_hz=0` restores the original instruction. Applies at endpoint creation. **Replug the device**. |

All sites are located by pattern with uniqueness checks; the three hid
sites must additionally all `bl` the same function (`Task::SetInterval`).
If anything is off the module logs and bails rather than writing.

### Adding a new hid build id

Text patching works on any firmware the patterns match. Live task poking
(rates change without waiting for the next activate / re-attach) is
build-id keyed. See `HID_LAYOUTS[]` in `source/hidpoll/source/patcher_hid.c`:

```c
static const HidLayout HID_LAYOUTS[] = {
    { "C3030310E47B3841417518902AFFB2301F0033FC",  // hid main-module build id
      0x26c000,   // rm_off        - ResourceManager singleton
      0x498,      // npad_task_off - RM + this = Npad periodic task
      0xbb0,      // ahid_base_off - RM + this = first AhidSampler thread
      0x368,      // ahid_stride
      17,         // ahid_count
      0x320 },    // ahid_task_off - thread object + this = periodic task
};
```

The build id is logged on every apply (`log.txt`) and shown in
`hidpoll_ctl`'s Status tab. Task period is always at `task+0x30`.

## IPC

Service name `hidpoll`, plain cmif (no domains), max 4 sessions. See
`source/hidpoll/include/hidpoll.h`. The header is standalone and
includes inline client helpers:

```c
Service s;
hidpollInitialize(&s);
hidpollSetRate(&s, 1000);
HidpollStatus st;
hidpollGetStatus(&s, &st);
hidpollExit(&s);
```

| cmd | name          | in      | out             |
|-----|---------------|---------|-----------------|
| 0   | `GetStatus`   | –       | `HidpollStatus` |
| 1   | `SetHidRate`  | `u32 hz`| –               |
| 2   | `SetUsbRate`  | `u32 hz`| –               |
| 3   | `SetRate`     | `u32 hz`| – (both)        |
| 4   | `Reapply`     | –       | –               |

All setters persist to `config.ini` and re-run the patcher immediately.
