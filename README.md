# RPCS3 for PS5

[RPCS3](https://rpcs3.net/), the PlayStation 3 emulator, as a homebrew title for the
PS5 (`PPSA99200`): both of its recompilers running on the console, Mesa's RADV
Vulkan driver ported to it, frame generation, and a launcher and pause menu made for
a controller.

> [!IMPORTANT]
> **Piracy is not condoned.** Nothing here contains, or will ever provide or link to,
> games, PS3 firmware, BIOS files or decryption keys. Install the PS3 system software
> from Sony's own `PS3UPDAT.PUP`, and play backups of games you own, made yourself.

> [!WARNING]
> **Source only. There is no download of the built title, and there will not be one.**
> RPCS3 is GPL-2.0-only, and the PS5 platform layer every title links is
> GPL-3.0-or-later: the two cannot be combined into one program that is shared. Build
> it for your own console, and **please don't share the builds** (`eboot.bin`,
> `rpcs3-code.bin` or the title folder).

## What it does

Measured on my console (build 150, the source of
[v0.1.0-beta.1](docs/releases/v0.1.0-beta.1.md)):

- Commercial PS3 games boot and play. An open-world game I test with ran 20 minutes,
  through its first missions to a save, at about 27 fps (about 45 shown with frame
  generation), with its memory flat; another ran at about 25 fps.
- **Frame generation**: the title's own, Lossless Scaling's (from your own
  `Lossless.dll`) or MihawkGen's neural network.
- **The launcher**: games from folders and ISOs, trophies, save data and save states,
  game patches, PS3 package installs, per-game settings and controller remapping,
  screenshots, and a pause menu with quick settings and a performance page.

Heavy games run below full speed: the console's CPU emulating the PS3's SPUs is the
limit. What works, what does not, and the known issues are in
[`rpcs3/README.md`](rpcs3/README.md).

## Building and installing

You need a Linux PC and a PS5 running a homebrew environment. The five repositories
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
| The platform layer | my fork of the PS5 payload SDK, [PS5_PayloadSDK](https://github.com/KongaTime/PS5_PayloadSDK) |
| Frame generation | the title's own; a port of Lossless Scaling's (from the Eden emulator's); [MihawkGen](https://github.com/KongaTime/PS5_MihawkGen) |
| `rpcs3/` here | the build of RPCS3, LLVM, FFmpeg and libiconv for the console, and the program that starts RPCS3 |

Built with AI writing much of the code: I directed it, tested every build on my
console, and decided what shipped.

## Licences

RPCS3 is GPL-2.0-only; the platform layer, the Lossless Scaling port and MihawkGen
are GPL-3.0-or-later; the template foundation is MIT (`LICENSE.md`, Sascha Willems',
with the PS5 layer under the same licence); the assets keep their own
(`ps5/ASSETS.md`). Because GPL-2.0-only and GPL-3.0 cannot be combined in a shared
program, the title is published as source only. Your own `Lossless.dll` is read on
your console and never copied or shipped.

Not affiliated with or endorsed by Sony Interactive Entertainment or the RPCS3 team.
"PlayStation", "PS3" and "PS5" are trademarks of Sony Interactive Entertainment Inc.
