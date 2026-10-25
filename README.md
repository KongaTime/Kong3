# Kong³

[RPCS3](https://rpcs3.net/), the PlayStation 3 emulator, as a homebrew title for the
PS5 (`PPSA99200`), by kongatime: both of its recompilers running on the console, Mesa's RADV
Vulkan driver ported to it, frame generation, and a launcher and pause menu made for
a controller.

> [!IMPORTANT]
> **Piracy is not condoned.** Nothing here contains, or will ever provide or link to,
> games, PS3 firmware, BIOS files or decryption keys. Install the PS3 system software
> from Sony's own `PS3UPDAT.PUP`, and play backups of games you own, made yourself.

> [!NOTE]
> **The built title is a download from v1.0.0-beta1 on; the latest is v1.0.0-beta2** ([releases](https://github.com/KongaTime/Kong3/releases)).
> Nothing in it is GPL-3.0-or-later any more ([`docs/distributable-build.md`](docs/distributable-build.md)):
> as a whole it is GPL-2.0-only, RPCS3's licence, and each release attaches its complete
> source. You may share it on those terms. A build of your own made with
> `PS5_GPL3_FRAME_GENERATION=1` or `PS5_RPCN=1` has GPL-3.0 code in it again: keep
> those to yourself.

## What it does

Measured on my console (build 150, the source of
[v0.1.0-beta.1](docs/releases/v0.1.0-beta.1.md)):

- Commercial PS3 games boot and play. An open-world game I test with ran 20 minutes,
  through its first missions to a save, at about 27 fps (about 45 shown with frame
  generation), with its memory flat; another ran at about 25 fps.
- **Frame generation**: the title's own. Lossless Scaling's (from your own
  `Lossless.dll`) and MihawkGen's neural network are GPL-3.0, so only in a build of
  your own (`PS5_GPL3_FRAME_GENERATION=1`), as is online play over RPCN
  (`PS5_RPCN=1`, its TLS is GPL-3.0).
- **The launcher**: games from folders and ISOs, trophies, save data and save states,
  game patches, PS3 package installs, per-game settings and controller remapping,
  screenshots, and a pause menu with quick settings and a performance page.

Heavy games run below full speed: the console's CPU emulating the PS3's SPUs is the
limit. What works, what does not, and the known issues are in
[`rpcs3/README.md`](rpcs3/README.md).

## Building and installing

The download: unpack it, copy `PPSA99200/` to the console's
`/data/homebrew/PPSA99200/` and register it (installing and adding your games:
[`rpcs3/README.md`](rpcs3/README.md#installing-and-running)).

To build it yourself, you need a Linux PC and a PS5 running a homebrew environment. The five repositories
of a release, side by side, the build commands, installing on the console and adding
your games: [`rpcs3/README.md`](rpcs3/README.md#building). The first build takes
hours (it builds LLVM for the console).

## Reporting problems

Open an issue with `rpcs3/cache/RPCS3.log` and `rpcs3-trace.txt` from the title's
folder, copied before you launch again, and what you saw. Never attach games,
firmware, keys or saves, and please don't ask for them.

## How it is made

| Part | From |
| --- | --- |
| The emulator | [RPCS3](https://github.com/RPCS3/rpcs3), by the RPCS3 team, in my fork [PS5_RPCS3](https://github.com/KongaTime/PS5_RPCS3) with the PS5 frontend in `rpcs3/ps5/` |
| The title around it | the [PS5 Vulkan Template](https://github.com/mihawk-99/PS5_VulkanTemplate) foundation (at `f827f0a5`): Sascha Willems' Vulkan example base and its PS5 layer, which launches the title, reads the controllers and builds it (`ps5/`) |
| The Vulkan driver | RADV, from [PS5_Mesa](https://github.com/KongaTime/PS5_Mesa), built by [PS5_Vulkan](https://github.com/KongaTime/PS5_Vulkan) |
| The platform layer | the PS5 libre layer, this title's own (`ps5/libre/`, MIT): start-up, memory, threads and the C library's gaps; the toolchain and headers from my fork of the PS5 payload SDK, [PS5_PayloadSDK](https://github.com/KongaTime/PS5_PayloadSDK) |
| Frame generation | the title's own; in builds of your own, a port of Lossless Scaling's (from the Eden emulator's) and [MihawkGen](https://github.com/KongaTime/PS5_MihawkGen) |
| `rpcs3/` here | the build of RPCS3, LLVM, FFmpeg and libiconv for the console, and the program that starts RPCS3 |

Built with AI writing much of the code: I directed it, tested every build on my
console, and decided what shipped.

## Licences

RPCS3 is GPL-2.0-only, and so is the title as a whole; the PS5 libre layer and the
template foundation are MIT (`LICENSE.md`, Sascha Willems', with the PS5 layer under
the same licence); the assets keep their own (`ps5/ASSETS.md`). Every part in the
title, with its licence and source, is listed in its `licenses/README.txt`; each
release attaches the source. The Lossless Scaling port, MihawkGen and wolfSSL
(RPCN's TLS) are GPL-3.0-or-later and are left out of the download
(`ps5/tools/shareable.py` checks each link). Your own `Lossless.dll` is read on your
console and never copied or shipped.

Not affiliated with or endorsed by Sony Interactive Entertainment or the RPCS3 team.
"PlayStation", "PS3" and "PS5" are trademarks of Sony Interactive Entertainment Inc.
