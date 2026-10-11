/*
 * RPCS3 for the PS5 - the title's program: RPCS3's PS5 frontend.
 *
 * ps5/src/main.cpp calls this in place of the samples once the platform layer
 * is up (klog, the pad, the splash) and volk points at the RADV linked in.
 * The frontend is my fork's (PS5_RPCS3, rpcs3/ps5/ps5_frontend.h), linked from
 * its archives (rpcs3/rpcs3.cmake). It reads the controllers through this
 * title's pad API (poll_pads, below), and ends a fatal error through this
 * title's catchReturnFromMain.
 *
 * Each step of the start, and RPCS3's warnings and errors, go to
 * /app0/rpcs3-trace.txt as they happen (written through at once, so a crash
 * keeps them), readable over FTP without klog.
 *
 * What it boots: the first line of /app0/rpcs3-boot.txt (an ELF, or a game's
 * folder), if there is one; else RPCS3 starts, reports the PS3 system software
 * it finds, and stops.
 *
 * Copyright (C) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 */

#include "platform.h"

#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <mutex>
#include <string>

/* The fork's interface (rpcs3/ps5/ps5_frontend.h), as it declares it */
struct rpcs3_ps5_pad {
	bool connected;
	uint32_t buttons;
	float left_x, left_y, right_x, right_y;
	float l2, r2;
};
enum : uint32_t {
	RPCS3_PS5_UP = 1u << 0, RPCS3_PS5_DOWN = 1u << 1, RPCS3_PS5_LEFT = 1u << 2, RPCS3_PS5_RIGHT = 1u << 3,
	RPCS3_PS5_CROSS = 1u << 4, RPCS3_PS5_CIRCLE = 1u << 5, RPCS3_PS5_SQUARE = 1u << 6, RPCS3_PS5_TRIANGLE = 1u << 7,
	RPCS3_PS5_L1 = 1u << 8, RPCS3_PS5_R1 = 1u << 9, RPCS3_PS5_L3 = 1u << 10, RPCS3_PS5_R3 = 1u << 11,
	RPCS3_PS5_START = 1u << 12, RPCS3_PS5_SELECT = 1u << 13,
};
constexpr int rpcs3_ps5_pad_players = 4;
struct rpcs3_ps5_title {
	void (*poll_pads)(rpcs3_ps5_pad pads[rpcs3_ps5_pad_players]);
	void (*trace)(const char *line);
	const char *build;
};
int rpcs3_ps5_run(const char *boot_path, const rpcs3_ps5_title &title);

namespace {

/* This build's number, which the launcher shows and the trace and RPCS3.log
 * start with: one more for each build that goes to the console */
const char *const titleBuild = "133";

/* The console's buttons as the PS3's: OPTIONS is START, the touch pad's click
 * SELECT; the PS button stays the shell's */
uint32_t ps3Buttons(uint32_t held)
{
	static const struct { uint32_t pad, ps3; } map[] = {
		{ PAD_UP, RPCS3_PS5_UP }, { PAD_DOWN, RPCS3_PS5_DOWN }, { PAD_LEFT, RPCS3_PS5_LEFT },
		{ PAD_RIGHT, RPCS3_PS5_RIGHT }, { PAD_CROSS, RPCS3_PS5_CROSS }, { PAD_CIRCLE, RPCS3_PS5_CIRCLE },
		{ PAD_SQUARE, RPCS3_PS5_SQUARE }, { PAD_TRIANGLE, RPCS3_PS5_TRIANGLE }, { PAD_L1, RPCS3_PS5_L1 },
		{ PAD_R1, RPCS3_PS5_R1 }, { PAD_L3, RPCS3_PS5_L3 }, { PAD_R3, RPCS3_PS5_R3 },
		{ PAD_OPTIONS, RPCS3_PS5_START }, { PAD_TOUCH_PAD, RPCS3_PS5_SELECT },
	};
	uint32_t buttons = 0;
	for (const auto &entry : map)
		if (held & entry.pad)
			buttons |= entry.ps3;
	return buttons;
}

/* RPCS3's pad thread reads every player at once */
void pollPads(rpcs3_ps5_pad pads[rpcs3_ps5_pad_players])
{
	struct pad first;
	pad_poll(&first);
	const uint32_t connected = pad_players();
	for (int player = 0; player < rpcs3_ps5_pad_players; player++) {
		struct pad in{};
		pad_player(player, &in);
		rpcs3_ps5_pad &out = pads[player];
		out = {};
		out.connected = (connected >> player) & 1;
		/* While the shell has the pad (the home screen, a dialog) the game gets nothing */
		if (!out.connected || (in.held & PAD_INTERCEPTED))
			continue;
		out.buttons = ps3Buttons(in.held);
		out.left_x = in.left_x;
		out.left_y = in.left_y;
		out.right_x = in.right_x;
		out.right_y = in.right_y;
		out.l2 = in.l2;
		out.r2 = in.r2;
	}
}

/* /app0/rpcs3-trace.txt, written through line by line; RPCS3 calls it from any thread */
const char *const tracePath = "/app0/rpcs3-trace.txt";
std::mutex traceLock;

void trace(const char *line)
{
	std::lock_guard lock(traceLock);
	if (FILE *file = fopen(tracePath, "a")) {
		fprintf(file, "%9.3f %s\n", now_seconds(), line);
		fclose(file);
	}
	say("RPCS3: %s", line);
}

} // namespace

// RADV's shader cache (Mesa's multipart database, /app0/radv-shader-cache)
// keeps two files open for each of its parts in each cache, 50 parts by
// default, and a title holds about 249 files open by path at once
// (PS5_PayloadSDK's platform/docs/PROBE.md). On the console the cache had
// grown into every part: about 200 files were open before a game opened one,
// and GTA IV, then R&C and X-Men, failed in sys_fs_open with EMFILE (the
// census of 496fa5f: 4 open in each part). Four parts hold about 16. Set
// before anything creates a Vulkan device, in a static initialiser, ahead of
// the template's own start
namespace
{
	const int radv_cache_parts = []
	{
		setenv("MESA_DISK_CACHE_DATABASE_NUM_PARTS", "4", 0);
		return 4;
	}();
}

extern "C" int ps5_title_main(void)
{
	/* Kept from a fatal error before main (RPCS3's static initialisers), if any */
	if (FILE *file = fopen(tracePath, "a")) {
		fprintf(file, "---- launch\n");
		fclose(file);
		chmod(tracePath, 0666);
	}
	trace((std::string("title: start, build ") + titleBuild).c_str());
	std::string boot;
	if (std::ifstream file{"/app0/rpcs3-boot.txt"}) {
		std::getline(file, boot);
		while (!boot.empty() && (boot.back() == '\r' || boot.back() == ' '))
			boot.pop_back();
	}
	trace(boot.empty() ? "title: starting RPCS3 without a game" : ("title: booting " + boot).c_str());
	const rpcs3_ps5_title title{ pollPads, trace, titleBuild };
	const int status = rpcs3_ps5_run(boot.c_str(), title);
	trace(("title: RPCS3 stopped, status " + std::to_string(status)).c_str());
	return status;
}
