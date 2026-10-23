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

#include <regex.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <locale>
#include <sstream>
#include <cstdlib>
#include <mutex>
#include <string>

/* After libc++'s <locale>: the SDK's xlocale headers, first, hide its narrow _l classifiers */
#include <execinfo.h>
#include <langinfo.h>
#include <xlocale.h>

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
const char *const titleBuild = "170";

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

/* The PS5 libre layer's regular expressions and memory streams (builds 166 to 168), the C locale and backtraces (169, 170),
 * tried once at the start: what each gave, to the trace */
void checkLibre()
{
	char line[256];
	regex_t re;
	int result = regcomp(&re, "^([a-z]+)-([0-9]+)$", REG_EXTENDED);
	regmatch_t found[3]{};
	if (result == 0) {
		result = regexec(&re, "build-168", 3, found, 0);
		regmatch_t range[1] = { { 6, 11 } };
		const int ranged = regexec(&re, "xxxxxxabc-1yyy", 1, range, REG_STARTEND);
		snprintf(line, sizeof line,
			"libre: regex %d, groups %lld-%lld %lld-%lld, REG_STARTEND %d at %lld-%lld (expected 0, 0-5 6-9, 0 at 6-11)",
			result, (long long)found[1].rm_so, (long long)found[1].rm_eo, (long long)found[2].rm_so,
			(long long)found[2].rm_eo, ranged, (long long)range[0].rm_so, (long long)range[0].rm_eo);
		regfree(&re);
	} else {
		snprintf(line, sizeof line, "libre: regcomp failed: %d", result);
	}
	trace(line);

	char *buffer = nullptr;
	size_t size = 0;
	if (FILE *stream = open_memstream(&buffer, &size)) {
		fprintf(stream, "hello %d", 168);
		fflush(stream);
		const size_t flushed = size;
		fprintf(stream, " and more");
		const int closed = fclose(stream);
		snprintf(line, sizeof line, "libre: open_memstream %zu at fflush, %zu '%s' at fclose (%d) (expected 9, 18 'hello 168 and more', 0)",
			flushed, size, buffer ? buffer : "(null)", closed);
		free(buffer);
	} else {
		snprintf(line, sizeof line, "libre: open_memstream failed: errno %d", errno);
	}
	trace(line);

	/* stderr on the klog pipe (libre/klog.h), and descriptor 2 too if the console let it be */
	struct stat status{};
	const bool fd2Pipe = fstat(STDERR_FILENO, &status) == 0 && S_ISFIFO(status.st_mode);
	const int stderrFd = fileno(stderr);
	const bool streamPipe = fstat(stderrFd, &status) == 0 && S_ISFIFO(status.st_mode);
	snprintf(line, sizeof line, "libre: stderr on the klog pipe: %s (descriptor %d); descriptor 2 too: %s",
		streamPipe ? "yes" : "no", stderrFd, fd2Pipe ? "yes" : "no");
	trace(line);

	/* The C locale (build 169): C's _l calls, and libc++, which is built on them */
	locale_t c = newlocale(LC_ALL_MASK, "C", nullptr);
	locale_t other = newlocale(LC_ALL_MASK, "de_DE.UTF-8", nullptr);
	char *end = nullptr;
	const double parsed = strtod_l("0.62x", &end, c);
	char printed[32];
	snprintf_l(printed, sizeof printed, c, "%.3f", 1.25);
	snprintf(line, sizeof line,
		"libre: locale C %s, de_DE %s; strtod_l %.2f stops at '%s', snprintf_l %s, localeconv '%s', MB_CUR_MAX %zu, "
		"___mb_cur_max_l %d, CODESET %s, %s %s (expected yes, no; 0.62 'x', 1.250, '.', 1, 4, UTF-8, Jan Saturday)",
		c ? "yes" : "no", other ? "yes" : "no", parsed, end ? end : "?", printed, localeconv()->decimal_point,
		(size_t)MB_CUR_MAX, ___mb_cur_max_l(c), nl_langinfo(CODESET), nl_langinfo(ABMON_1), nl_langinfo(DAY_7));
	trace(line);

	std::ostringstream stream;
	stream << 0.62 << ' ' << 1234567;
	const std::ctype<wchar_t> &wide = std::use_facet<std::ctype<wchar_t>>(std::locale::classic());
	snprintf(line, sizeof line,
		"libre: libc++ stod %.2f, stream '%s', isalpha a %d 1 %d, toupper q %c, wide alpha 0xe9 %d (iswalpha %d), "
		"upper 0xe9 0x%x (towupper 0x%x) (expected 0.62, '0.62 1234567', 1 0, Q, the same twice)",
		std::stod("0.62"), stream.str().c_str(), std::isalpha('a', std::locale::classic()) ? 1 : 0,
		std::isalpha('1', std::locale::classic()) ? 1 : 0, std::toupper('q', std::locale::classic()),
		wide.is(std::ctype_base::alpha, wchar_t(0xe9)) ? 1 : 0, iswalpha(0xe9) ? 1 : 0,
		(unsigned)wide.toupper(wchar_t(0xe9)), (unsigned)towupper(0xe9));
	trace(line);

	const char *utf8 = "h\xc3\xa9llo";
	const char *from = utf8;
	mbstate_t state{};
	wchar_t wideText[16]{};
	const size_t wideCount = mbsnrtowcs_l(wideText, &from, 3, 16, &state, c);
	const wchar_t *wideFrom = wideText;
	char back[16]{};
	state = {};
	const size_t backCount = wcsnrtombs_l(back, &wideFrom, 2, sizeof back, &state, c);
	snprintf(line, sizeof line,
		"libre: mbsnrtowcs_l of 3 bytes %zd (0x%x 0x%x), wcsnrtombs_l of 2 %zd '%s' (expected 2 (0x68 0xe9), 3 'h\xc3\xa9' "
		"in UTF-8; in a single-byte locale 3 (0x68 0xc3 0xa9), 2 'h\xc3')",
		(ssize_t)wideCount, (unsigned)wideText[0], (unsigned)wideText[1], (ssize_t)backCount, back);
	trace(line);

	void *frames[16];
	const size_t depth = backtrace(frames, 16);
	snprintf(line, sizeof line, "libre: backtrace %zu frames, to klog:", depth);
	trace(line);
	backtrace_symbols_fd(frames, depth, STDERR_FILENO);
	if (c)
		freelocale(c);
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
	checkLibre();
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
