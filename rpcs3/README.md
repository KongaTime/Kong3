# RPCS3 on the PS5

This title's program is [RPCS3](https://rpcs3.net/), the PlayStation 3 emulator, from my
fork [PS5_RPCS3](https://github.com/KongaTime/PS5_RPCS3). RPCS3's developers make the
emulator; the fork adds what the console needs (`__PROSPERO__` and `PS5` in its sources
and CMake), and its frontend for the console, a launcher and a pause menu, is in
`rpcs3/ps5/` there.

> [!IMPORTANT]
> **Piracy is not condoned.** Nothing here contains, or will ever provide or link to,
> games, PS3 firmware, BIOS files or decryption keys. Install the PS3 system software
> from Sony's own `PS3UPDAT.PUP`, and play backups of games you own, made yourself.

> [!NOTE]
> **The built title, Kong³, is a download from v1.0.0-beta1 on.** Nothing in it is
> GPL-3.0-or-later; as a whole it is GPL-2.0-only, RPCS3's licence, and each release
> attaches its complete source. A build of your own with
> `PS5_GPL3_FRAME_GENERATION=1` (Lossless Scaling's and MihawkGen's frame generation)
> or `PS5_RPCN=1` (online play; its TLS is wolfSSL) has GPL-3.0 code in it: keep
> those to yourself.

## What works

Measured on my console, with build 150 (PS5_RPCS3 `d2b0ef4`):

- Commercial PS3 games boot and play, with both recompilers (PPU and SPU LLVM). One
  open-world game I test with played for 20 minutes, through its first missions to a
  save, at about 27 fps, with no crash and the heap flat at about 3 GiB. Another
  open-world game played 7.5 minutes at about 25 fps, its graphics thread flat at
  about 600 MiB. Both are CPU-bound: the emulated SPUs run at 85 to 100% of their
  cores.
- **Frame generation** shows frames made between the game's own: the title's own. In
  a build of your own with `PS5_GPL3_FRAME_GENERATION=1` (GPL-3.0, so not for
  sharing) also Lossless Scaling's (from your own `Lossless.dll`, below) and
  MihawkGen, a small trained network. With one frame made between, the 27 fps game
  showed about 45 frames a second; fast camera turns can smear or double edges.
- **A launcher**: the games in `rpcs3/games/` (folders and ISOs) with search, sort and
  favourites; trophies; save data; game patches (`patch.yml`); PS3 packages and
  licences from `rpcs3/packages/`; screenshots. **A pause menu** (L1 + R1 + the touch
  pad): quick settings, save states, controller remapping per game, a performance
  page.
- Each game's own settings (RPCS3's custom configurations), set in the launcher.
- Sound through the console's audio output; up to four controllers.

## What does not, or not yet

- **Speed.** Heavy games run well below full speed: the console's CPU emulates the
  PS3's Cell, and the SPUs are the limit. Frame generation makes motion smoother; it
  does not make the game itself faster.
- **First boots are slow.** A game's first boot compiles its code (several minutes,
  with the screen still or a progress screen), and its shaders compile as they first
  appear (stutter). Later boots load both from the cache.
- Some games freeze without **Accurate SPU Reservations** (on by default: leave it on).
- The graphics driver keeps up to 1 MiB for each of RPCS3's command buffers, about
  500 MiB in a long session.
- Untested in this release: most of the PS3's library. Report what you find.

## How the pieces fit

| Piece | Where | What |
| --- | --- | --- |
| RPCS3's emulator core and the PS5 frontend | PS5_RPCS3, beside this repository | `rpcs3_emu`, `rpcs3_ps5` and their libraries |
| `rpcs3/build-rpcs3.sh` | here | cross-builds the fork into `build/rpcs3/` (clang 19 or later) |
| `rpcs3/build-ffmpeg.sh` | here | FFmpeg 8.1.1 for the console (RPCS3's video and audio decoding) |
| `rpcs3/build-libiconv.sh` | here | GNU libiconv 1.18 for the console (`cellL10n`'s text encodings) |
| `rpcs3/build-llvm.sh` | here | LLVM 22.1 (the commit RPCS3 pins) for the console, X86 only, for the PPU and SPU recompilers; its changes in `rpcs3/llvm-patches/` |
| `rpcs3/code-copy.py` | here | `rpcs3-code.bin`, a readable copy of the title's code for RPCS3's fault handler (the console maps code execute-only) |
| `rpcs3/clang-scan-deps-ps5` | here | C++20 module scanning with the console compiler's flags (OpenAL Soft) |
| `rpcs3/rpcs3.cmake` | here | joins `title_main.cpp` to the title and RPCS3's archives to its link |
| `rpcs3/title_main.cpp` | here | the program: reads the controllers, starts RPCS3 |

RPCS3's Vulkan calls go through the foundation's volk to the RADV the title links. Its
files live in `/app0/rpcs3/` (the configuration, `dev_hdd0`, `games/`), its caches and its log
(`RPCS3.log`) in `/app0/rpcs3/cache/`, and each step of the start
with a status line every 5 seconds in `/app0/rpcs3-trace.txt`, which FTP can read.

## Building

You need a Linux PC (I use Arch and CachyOS) and a PS5 running a homebrew environment
(an enabler, ShadowMountPlus for `/data/homebrew`, an FTP server). The repositories,
side by side in one folder:

| Repository | Clone it as | Revision of this release |
| --- | --- | --- |
| [KongaTime/PS5_RPCS3Title](https://github.com/KongaTime/PS5_RPCS3Title) (this), branch `rpcs3-beta-1` | `PS5_RPCS3Title` | the release tag, `v0.1.0-beta.1` |
| [KongaTime/PS5_RPCS3](https://github.com/KongaTime/PS5_RPCS3), branch `rpcs3-beta-1` | `PS5_RPCS3` | `d2b0ef47b4179a66be285b40d7539ecb184ee9b2`, with its submodules (OpenCV's is not needed; LLVM's at depth 1 is enough) |
| [KongaTime/PS5_PayloadSDK](https://github.com/KongaTime/PS5_PayloadSDK) | `PS5_PayloadSDK` | `1de8b37d583de476ad07cde9a0d562b75fb1be8f` (`ps5/tools/setup-sdk.sh` pins it) |
| [KongaTime/PS5_Vulkan](https://github.com/KongaTime/PS5_Vulkan), branch `rpcs3-beta-1` | `PS5_Vulkan` | `f3cbf875f89978deae9f855b9be2610cec2818ae` |
| [KongaTime/PS5_Mesa](https://github.com/KongaTime/PS5_Mesa), branch `rpcs3-beta-1` | `PS5_Mesa` | `dc82d010021e4bae5a12cd143ff44db4d7307e47` (PS5_Vulkan's `tools/build-radv.sh` pins it) |

```bash
git clone -b rpcs3-beta-1 https://github.com/KongaTime/PS5_RPCS3Title
git clone -b rpcs3-beta-1 --recurse-submodules --shallow-submodules https://github.com/KongaTime/PS5_RPCS3
git clone https://github.com/KongaTime/PS5_PayloadSDK
git clone -b rpcs3-beta-1 https://github.com/KongaTime/PS5_Vulkan
git clone -b rpcs3-beta-1 https://github.com/KongaTime/PS5_Mesa
cd PS5_RPCS3Title
```

```bash
ps5/tools/bootstrap.sh --check            # what the PC is missing
ps5/tools/bootstrap.sh                    # builds the stack beside this repository (RADV: long)
rpcs3/build-rpcs3.sh rpcs3_ps5 Fusion     # FFmpeg, libiconv and LLVM first (LLVM: hours), then RPCS3
PS5_CLANG=clang-20 ps5/tools/build.sh     # the title, linked with RPCS3, in dist/PPSA99200/
```

`PS5_CLANG` names the clang whose compiler-rt the link takes: the same version the SDK
compiles with (the newest `llvm-config` it finds), or the link stops on a missing
`libclang_rt.builtins-x86_64.a`.

## Installing and running

1. Copy `dist/PPSA99200/` to the console's `/data/homebrew/PPSA99200/` over FTP
   (`ps5/tools/deploy.sh`), and register it with ShadowMountPlus.
2. Put Sony's `PS3UPDAT.PUP` (the PS3 update from PlayStation's own site) in
   `/data/homebrew/PPSA99200/` and launch the title: it installs the system software
   once.
3. Put your games, each a folder or an ISO you made from your own disc, in
   `/data/homebrew/PPSA99200/rpcs3/games/`; PS3 packages and their licences in
   `rpcs3/packages/`, installed from the launcher's Install tab.
4. For Lossless Scaling's frame generation, in a build of your own with
   `PS5_GPL3_FRAME_GENERATION=1`: your own copy of its `Lossless.dll` (from Steam) in
   `/data/homebrew/PPSA99200/rpcs3/`. It is read there, never copied or shipped.

Updating: replace `eboot.bin` and `rpcs3-code.bin`; your settings, saves, games and
caches stay.

## Reporting problems

Send `rpcs3/cache/RPCS3.log` and `rpcs3-trace.txt` from the title's folder, copied before
the next launch (a launch replaces them), with what you saw and when. Never attach
games, firmware, keys or saves.
