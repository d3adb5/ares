# End-to-end latency bench

Measures a game's present on the host to the panel's present fence on an Android client, per frame, without a camera.
Results, method and limits: [docs/latency-measurements.md](../../docs/latency-measurements.md). Written for Athena on a
Retroid Pocket 6 and an AMD host; other hardware needs the notes under "Assumed".

## Setup

```sh
gcc -O2 -o probe-app probe-app.c -lvulkan -lxcb -lm
gcc -O2 -I/usr/include/libdrm -o probe-client probe-client.c
# device clock helper, with the NDK; send it one byte per read
aarch64-linux-android33-clang -O2 -static -o clk clk.c && adb push clk /data/local/tmp/ares-clk
adb shell chmod 755 /data/local/tmp/ares-clk
```

Settings, from the environment (`env.sh`):

| Variable | Default | Meaning |
|---|---|---|
| `RSH` | `rsh` | Command that runs its stdin as root on the device. Retroid handhelds have no `su`; any root shell works. |
| `HOST_NAME` | `uname -n` | The host's name in Athena's PC list. |
| `ATHENA` | `local.retroid.athena` | Client package. |
| `GAMESCOPE` | `gamescope` | Binary for `headless.sh`. |

Test apps: `apps.example.json` has every app the scripts name by UUID. Use a throwaway config, not your own:

```sh
cfg=$(mktemp -d)/ares; mkdir -p "$cfg"
sed "s#@BENCH@#$PWD#g" apps.example.json > "$cfg/apps.json"
cp -r ~/.config/ares/{credentials,ares_state.json} "$cfg"/   # pairing: delete afterwards
printf 'hermes_kms_isolated_sessions = true\ngamepad = switch\n' > "$cfg/ares.conf"
systemctl --user stop ares
XDG_CONFIG_HOME=${cfg%/ares} HERMES_GAMESCOPE=/usr/lib/hermes-gamescope/bin/gamescope ../../build/sunshine
```

The dev build has no capabilities, so no private /dev and no gamepads, and it logs fatal-looking errors about KMS
capture and finding a display; the stream route doesn't need either. Restart `ares` when done. Pair the client first.
The device must be unlocked: `kgwait.sh` waits for that.

## Scripts

Latency, one stream each:

| Script | Prints |
|---|---|
| `run-sparse.sh <label> [uuid]` | Steady latency: the last frame before each 100 ms pause (a marker every 3-6 s). Best measure of a running game. |
| `run-e2e.sh <label> <uuid>` | Latency per marker with gaps every 150-500 ms (`probe-app -120`). Pauses disturb the pacing being measured: use for comparing routes. |
| `run-steady.sh <label> <uuid>` | Panel vs host frame counts over 14 s, new frames per vsync. |

A real game: `run-game.sh <label> <uuid> <process name>`, with `GAMESCOPE_WSI_PRESENT_LOG=<this directory>/host-present.log` in its env (a Gamescope
with the layer's present log). It freezes the game for 100 ms every 3-6 s, so leave it at a menu or standing still.

Sets of them: `run-set.sh`, `run-mode.sh <tag>` (adds HWC misses), `matrix.sh <uuid> <tag> <rounds> <fps...>`, `cell.sh`,
`vsync-matrix.sh` (swapchain images and vsync off by stream rate), `load-matrix.sh` (GPU-bound; writes `load.n`),
`cap-test.sh` (the image cap on an app asking for three).

The client and device: `setfps.sh <fps>`, `setpace.sh keep|latency|...`, `setdouble.sh true|false`, `sfset.sh <percent>|off` (bind an edited lead table
and restart SurfaceFlinger, which restarts the framework; `off` undoes it; a reboot also does).

Display: `panelrate.sh` (during a stream: hardware vsync counter and present fences), `vsynccount.sh <seconds> <label>`,
`display-probe.sh [seconds]` (read-only: a device's modes, lead table, rates; run this on a new device first).

Power and load: `powercell.sh <label> <fps> <mode>` and `sample.sh <seconds>` pause charging while they run and restore it.
`uitest.sh`, `uijank.sh <label>` drive the shade and recents and print SurfaceFlinger's misses and gfxinfo. `headless.sh <load>`:
game fps with nothing streaming. `gpu.sh`: host GPU busy.

Analysis, on the logs the scripts leave here (`hp-<label>.log`, `sf-<label>.txt`): `align.py`, `sparse.py`, `shown.py`,
`gaps.py`, `fps.py`, `e2e.py`, `stats.py`, `cpu.py`, `metric.py` (Ares's own encode and bitrate numbers). `clockoff.py`: device
CLOCK_MONOTONIC minus the host's, over USB. `probe-client.c`, `socket-input.py`: the Gamescope stream socket without Ares
(fence timing, direct input). amdgpu refuses to mmap those buffers, so pixels cannot be read on the CPU.

## Probe modes

`probe-app <fps> <fifo|mailbox|immediate> <log>` draws a counter and logs `counter present_ns`. Negative fps adds markers.
Wrappers: `probe-pan` (encodes like a game), `probe-noise` (encoder's worst case), `probe-sparse` (100 ms pauses),
`probe-still`, `probe-f2` (two images), `probe-load` / `probe-loadf2` / `probe-loadf3` (GPU load from `load.n`, one large
command buffer per frame), `probe-rate` (period from `period.ns`). Fps 1000 with FIFO is a free-running vsync game. The present
is logged before GPU work runs, so under load present to panel includes the app's own frame.

## Assumed

- `shown.py` and `sparse.py` hold the RP6's period (8.35 ms): change it for another panel.
- `panelrate.sh`, `vsynccount.sh`: `intf:1 ... mode: command` in debugfs. `display-probe.sh` finds another counter by itself.
- `sample.sh`: /sys/class/qcom-battery/restrict_chg. It does not stop USB feeding the device, so battery current only
  compares settings.
- `sfset.sh`: Qualcomm's `advanced_sf_offsets.xml`, at 120 Hz.
- `gpu.sh`: card1; `headless.sh`: `--prefer-vk-device 1002:744c`.
