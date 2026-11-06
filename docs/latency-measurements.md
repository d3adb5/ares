# Latency measurements (2026-11-05)

What a stream costs from a game's present to the client's panel, what each setting changes, and where it stops being
worth chasing. To repeat any of it: [tools/latency](../tools/latency/README.md).

Host: RX 7900 GRE (VCN4), wired. Client: Retroid Pocket 6 with [Athena](https://github.com/d3adb5/athena), Snapdragon 8 Gen 2,
6 GHz Wi-Fi, 1080p120, 75 Mbit/s. Other clients were not measured.

## Method

**Present to panel.** A Vulkan test app logs each present (host CLOCK_MONOTONIC). `dumpsys SurfaceFlinger --latency` gives each
frame's present fence (device CLOCK_MONOTONIC). The two clocks are tied by a helper over USB, reading the device clock one
byte at a time; the clocks drift about 40 ppm. Frames are matched by pauses in the app: a 100 ms pause every 3-6 s, using the
last frame before each pause, so the pacing being measured is not disturbed. Earlier marker methods with short, frequent gaps
gave misleading numbers.

**What it leaves out.** The controller to game path, the game's own simulation and render, and the panel's response (about 4
ms). No camera was used.

**Real games.** Gamescope's layer logs each present (`GAMESCOPE_WSI_PRESENT_LOG`, written where DXVK or the game hands the frame to
Vulkan), and `run-game.sh` freezes the process with SIGSTOP for 100 ms every 3-6 s as the marker.

**What it can't see.** Time before the present. A pacer that starts frames later (dxvk-low-latency) gains there, so its benefit
is not fully visible here. Static and panning content has smaller frames than a heavy game, so airtime is below a real game's.

## Is it enough?

Published figures, cruder than the numbers below, and mostly about network delay on top of a local game:

| Source | Finding |
|---|---|
| [Claypool, Latency Can Kill](https://sites.cs.ucsb.edu/~almeroth/classes/W10.290F/papers/claypool-10.pdf) | First person shooting degrades around 100 ms; third person avatars show measurable loss far later (around 500 ms) |
| [Claypool, taxonomy for cloud games](https://discmaster.textfiles.com/file/11697/ftp.cs.wpi.edu.zip/ftp.cs.wpi.edu/pub/techreports/pdf/14-07.pdf) | About 25% quality lost per 100 ms in cloud gaming (from the search summary only) |
| [Choy et al.](https://cs.uwaterloo.ca/~bernard/edgecloud) | 100 ms noticed; 80 ms network threshold |
| [Lampe et al.](https://www.kom.tu-darmstadt.de/papers/LWH+13.pdf?download) | Dick et al.: 50-100 ms tolerable; cloud services add 40-150 ms |
| [Regensburg placebo latency thesis](https://wiki.mi.uni-r.de/arbeiten/placebolatency) | 20-25 ms the lowest noticeable in the literature it cites, up to 50 ms |
| [Jitter study](https://core.ac.uk/download/76389185.pdf), [WPI](https://web.cs.wpi.edu/~claypool/iqp/delay-jitter-cloud-26/) | Jitter hurts apart from the average delay |

What games expect: consoles at 60 fps run about 40-67 ms from button to display, at 30 fps 100 ms or more and up to about 157
ms ([EventHubs](https://eventhubs.com/news/2009/sep/07/measuring-input-latency-games), [a later
comparison](https://cobra-error.pages.dev/posts/battlefield-vs.-call-of-duty-which-games-have-more-input-lag-)). These sources
are secondary and rough, but this is the range action games are tuned and played in.

No frame data was found for parries or dodges in TUNIC or Kingdom Hearts. Kingdom Hearts' physics depend on its frame rate on
PC, with 60 or 30 fps the usual advice ([1](https://gamerant.com/kingdom-hearts-hd-remix-pc-review/),
[2](https://gameplay.tips/fixed-solved/kingdom-hearts-hd-1-52-5-remix-common-issues-and-how-to-fix-them.html)), and a
speedrunning guide says TUNIC's physics tick follows the display frequency ([guide](https://www.speedrun.com/tunic/guides/69166),
search summary only).

Where this stream lands, estimated: 34 ms present to panel at 120 fps, a frame or two inside the game (8-16 ms), about 4 ms of
panel, plus an unmeasured input path of a few ms: about 50-60 ms. At 60 fps, about 65-85 ms. So a stream at 120 fps sits inside
the range of a console game at 60 fps played locally, and at 60 fps near its top.

**Conclusion.** Below this point, removing a few ms from a mean is not worth a stability cost. The last 4 ms of SurfaceFlinger's
lead is below any threshold above and cost visible late frames, so it stays at stock. The things still worth attention, in
order: (1) hitches and jitter, since a lost frame costs more than a few ms of average, (2) queued frames, which were the largest
single term left and cost nothing to remove, (3) Wi-Fi tail spikes. Input path and real-game latency remain unmeasured.

## Routes

Present to panel, 120 fps, 2-4 runs per row, lowest-latency pacing at stock lead:

| Route, codec, content | Present to panel, ms | Client fps of the host's ~108 | Host encode, ms | Mbit/s |
|---|---|---|---|---|
| Hermes-KMS, HEVC, static | 28.9-29.9 | 96-102 | | |
| Gamescope stream, HEVC, static | 23.9-24.9 | 99-101 | | |
| Hermes-KMS, HEVC, pan | 29.3-30.4 | 96-98 | 1.74 | 41 |
| Gamescope stream, HEVC, pan | 24.5-25.7 | 94-99 | 1.69 | 38.5 |
| Gamescope stream, AV1, pan | 24.5-24.7 | 97-99 | 1.50 | 32-33 |
| Hermes-KMS, HEVC, noise (1 run) | 34.2 | 53 | | |
| Gamescope stream, HEVC, noise | 23.7-29.5 | 52-54 | 1.96 | 88 |
| Gamescope stream, AV1, noise | 25.6-27.0 | 75-83 | 1.80 | 53 |

GPU-bound test app, 120 fps asked. Its present is logged before its GPU work, so about 11 ms of its own frame is included:

| Route, conversion | Present to panel, ms | Game fps | Host encode, ms |
|---|---|---|---|
| Hermes-KMS, shader | 47-48 | 89.4 | 7.8-8.3 |
| Stream, shader | 45-51 | 90-91 | 10.5 |
| Stream, on the encoder (EFC) | 35.2-35.6 | 91.5 | 1.64 |

Game fps with nothing streaming (`gamescope --backend headless`), at two loads: 61.4 and 118.3. Streaming matched both within
noise, so the routes cost a GPU-bound game no measurable frame rate. EFC was only measured at the middle load, and not with a
real game. One huge command buffer is the worst case for waiting behind the game, so 11 ms is an upper bound.

Composite wait alone, game present to composited, p50: fixed refresh timer vs composite on commit: 23 fps 4.52 to 0.38, 60 fps
FIFO 6.71 to 0.42, 90 fps 4.14 to 0.43, 120 fps mailbox 6.14 to 0.34.

Where about 24 ms goes: composite 0.35, encode 1.7, network and decode about 8, **MediaCodec release to present about 14**
(SurfaceFlinger's 10.33 ms lead, plus 4.17 ms on average to reach its next wake-up).

## Codecs

Nothing beats HEVC on latency. AV1 ties it at about 15% fewer bits and gets more frames through under noise, but VCN4 codes
1080p as 1082 lines, which shows two black rows at the bottom on the RP6. PyroWave needs too much bandwidth at this rate. No
VMAF was taken, so equal bitrates are not shown to be equal quality.

## Frame pacing: `keep`

A panel shows one frame per refresh. Android's compositor picks the newest frame ready when it wakes and discards anything
older. With lowest-latency pacing a frame is released as soon as it is decoded, so two frames that land between two wake-ups
replace each other, and the next refresh has nothing new. The host's frames and the panel's refreshes slide past each other, so
this comes in bursts of up to 26 a second.

`keep` (Athena's default) gives each frame its own refresh. Each is released with a target present time of the previous target
plus the panel's measured period, or the earliest it can make if that is later. If it falls more than a refresh behind, it starts
over from now, so a delay can't build up into a queue.

| Pacing, stock lead, 120 fps | Vsyncs with a new frame | Steady latency, ms |
|---|---|---|
| Lowest latency | 90-93% | 23.5 |
| `keep` | 98-99.9% | 25.8-26.8 |

Battery drain did not change measurably: net 1187-1344 mW across all runs, with identical runs differing as much as settings.

## The panel's real rate

The RP6's panel reports 120.00 Hz and runs at 119.71-119.77 Hz. It is a command-mode panel with its own oscillator. Three sources
agree: a hardware vsync counter (14377 and 14374 in about 120 s), present fences (119.71-119.755) and Choreographer (119.71-119.75).
The device clock is only about 40 ppm from the host's, against more than 2000 ppm here. The rate moves about 0.04 Hz within an
afternoon, so it has to be measured rather than stored, which is why `keep` samples it for 3 s each minute.

So a 120.00 fps host has to lose one frame about every 3.5 s on this panel.

| Host rate, pacing | Vsyncs with a new frame |
|---|---|
| 120.00, lowest latency | 90-93% |
| Panel's rate, lowest latency | 99-99.8% or 72-75%, depending on phase |
| 120.00, `keep` | 98.0-99.8% |
| Panel's rate, `keep` | 98.6-100% (100.0% in 6 of 10) |

Asking for the precise rate does not replace `keep`: alone it depends on a phase it does not control. On top of `keep` it adds
little, and games offer 120, not 119.71. Ares honours a client's fractional rate (`CONFIGURE refresh_mhz`), but it isn't needed.

## SurfaceFlinger's lead

How long before a present SurfaceFlinger starts composing. Set by Qualcomm's
`/vendor/etc/display/advanced_sf_offsets.xml`, under `vendor.display.target.version`, as a percentage of a refresh: on the RP6
124% at 120 Hz, or 10.33 ms. `debug.sf.late.sf.duration` does not change it. It is read when SurfaceFlinger starts.

| Lead, ms | Latency | HWC misses |
|---|---|---|
| 10.33 (stock) | | 0-1 a minute |
| 6.33 | -4 ms | about 30 a minute |
| 5.33 | | 2-3 a second |
| 4.33 | | 7 a second |

In the Android UI at 6.33 ms: SurfaceFlinger's misses +91 HWC and +105 GPU against +4 and +24, System UI jank 1.61% against 1.20%.
Power unchanged. Native games on the device were not tested.

Decision: stock. It is an option in Retroid Tools (Settings > Display latency), since it
needs root and affects everything on the device, not only streams. Not measured on any other device.

## Vsync in the game, and queued frames

A game with vsync on and no frame limiter renders as fast as it may and blocks on the swapchain. Under FIFO, images past two hold
finished frames waiting their turn, each a whole refresh old by the time it is shown. Games often ask for three, and DXVK for four. A game
that paces itself, or is GPU-bound, never builds that queue.

Free-running vsync game, `keep`, present to panel, ms:

| Stream rate | 3 images | 2 images | Vsync off | Own limiter at the stream's rate |
|---|---|---|---|---|
| 120 fps | 41.7-42.3 | 32.3-34.7 | 29.2-33.9 | 25.6-28.8 |
| 60 fps | 57.5-58.6 | 41.1-42.1 | 35.4-35.5 | 25.8-27.2 |

Each cell is two runs of about 10 samples each, so differences under 2-3 ms mean little. The cost is a refresh, so it grows at
lower rates. GPU-bound, the frame rate was the same with 2 and 3 images (90.5-91.4, 61.3-61.5)
and so was latency: double buffering did not halve the frame rate, since Gamescope isn't waiting for a scanout.

A real game need not behave like the test app. TUNIC, Proton CachyOS 11.0 (DXVK), main menu, 120 fps stream, present to panel, ms, about
16 samples per run; every cell held 120 fps:

| Configuration | Images | Present to panel | Lowest |
|---|---|---|---|
| Default | 4 | 31.8, 33.3 | 23-25 |
| Double buffering on | 2 | 31.6 | 26 |
| Low-latency DXVK | 4 | 43.6, 43.8 | 39-40 |
| Low-latency DXVK, double buffering on | 2 | 34.4 | 27 |

Four images did not queue like the test app: the default already matches the test app at two. Why was not established (the game's
own limiter or DXVK's frame latency limit are candidates, and TUNIC's vsync state was not read). Low-latency DXVK added 11 ms after the
present with four images, and about 3 ms with two. Time it saves before the present can't be seen by this method, so this does not
show whether it is better or worse from input to panel.

**Image counts.** DXVK asks for the surface's minimum image count plus one, whatever the game's buffer setting, which only sizes the
D3D side and limits its frame latency. Gamescope's layer reports a minimum of 3, so DXVK asks for 4. An in-game double or triple
buffering option therefore doesn't reach the swapchain in a D3D11 game under Proton. `dxgi.numBackBuffers` no longer exists.

**Options.** Ares sets nothing by default: games keep what they ask for. Limiting to two images is opt-in, per host
(`vsync_double_buffer`) or per client (Athena's "Limit vsync to double buffering", `vsyncDoubleBuffer=1` at launch); an app's `env` still
takes precedence. An app asking for three then measured 34.3-34.6 ms at 120 fps and 41.4-42.6 at 60, and a vsync-off app 31.2-31.4 at 120.
OpenGL games not going through the layer aren't limited (not measured). A game with vsync off doesn't tear under Gamescope, which
composites whole frames; a frame limiter must match the client's rate.

**Proton.** Proton CachyOS ships [dxvk-low-latency](https://github.com/netborg-afps/dxvk-low-latency) (`PROTON_DXVK_LOWLATENCY=1`, no game
support needed) and [vkd3d-low-latency](https://github.com/netborg-afps/vkd3d-low-latency) (`PROTON_VKD3D_LOWLATENCY=1`, the game must use Reflex
or waitable swapchains). Set in `apps.json`'s global `env`, both reach the game through Steam: seen in the process's environment, and the
prefix's `d3d11.dll` and `d3d12core.dll` change to the low-latency builds and back. A D3D12 game was not run.

## Not measured

- The controller to game path, and so whether low-latency DXVK helps overall. A real game in play rather than at a menu.
- Any client other than the RP6 (Odin 2, Odin 3, Pocket Nova): `tools/latency/display-probe.sh` prints a device's rates and table.
- Native games and emulators on the device with a shorter lead, and the panel's 60 Hz mode.
- The 1082-line AV1 fix, HDR on the stream route, and frame quality (VMAF).
