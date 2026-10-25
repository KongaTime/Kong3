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

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <locale>
#include <sstream>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/* After libc++'s <locale>: the SDK's xlocale headers, first, hide its narrow _l classifiers */
#include <execinfo.h>
#include <libre/threads.h>
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
const char *const titleBuild = "186";

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

/* Army of Two: The 40th Day froze the whole emulator for 2.7 s every 3 to 4 s
 * (build 176), each freeze starting at a file call while the game wrote its
 * disk cache and logged some 130 warnings a second. Where /app0/rpcs3-probe-io.txt
 * is: standard error at that rate, then the cache's pattern (1 MiB written,
 * read back in 64 KiB opens), each call timed, beside a thread that measures
 * how long the process stood still */
namespace {

double secondsSince(std::chrono::steady_clock::time_point start)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct probeTimes {
	double max = 0, total = 0;
	int count = 0, slow = 0;
	void add(double seconds)
	{
		max = seconds > max ? seconds : max;
		total += seconds;
		count++;
		slow += seconds > 0.1;
	}
	std::string text(const char *name) const
	{
		char line[160];
		snprintf(line, sizeof line, "%s %d, longest %.1f ms, %d over 100 ms, %.2f s in all", name, count, max * 1000, slow, total);
		return line;
	}
};

void probeIo()
{
	if (access("/app0/rpcs3-probe-io.txt", F_OK) != 0)
		return;
	std::atomic<bool> stop{false};
	std::atomic<double> stood{0};
	std::thread ticker([&] {
		auto last = std::chrono::steady_clock::now();
		while (!stop) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			const double gap = secondsSince(last);
			last = std::chrono::steady_clock::now();
			if (gap > stood)
				stood = gap;
		}
	});

	/* 15 s of standard error at 200 lines a second, each about 150 characters */
	probeTimes writes;
	const auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 3000; i++) {
		const auto before = std::chrono::steady_clock::now();
		fprintf(stderr, "probe-io %4d sys_fs: sys_fs_open(path=\"/dev_hdd1/FIOSCACHE/cach0000.dat\", flags=00, fd=*0xd008cfe0, mode=00, arg=*0x0, size=0x0)\n", i);
		writes.add(secondsSince(before));
		const double due = (i + 1) / 200.0;
		const double now = secondsSince(start);
		if (now < due)
			std::this_thread::sleep_for(std::chrono::duration<double>(due - now));
	}
	trace(("probe-io: standard error: " + writes.text("lines") + "; the process stood still up to " + std::to_string(int(stood * 1000)) + " ms").c_str());
	stood = 0;

	/* 40 files of 1 MiB, each written in 64 KiB parts, then read back with an
	 * open, a read and a close for each part */
	mkdir("/app0/rpcs3/probe-io", 0777);
	static char block[65536];
	probeTimes creates, parts, opens, reads, closes;
	for (int file = 0; file < 40; file++) {
		char path[96];
		snprintf(path, sizeof path, "/app0/rpcs3/probe-io/cach%04X.dat", file);
		auto before = std::chrono::steady_clock::now();
		FILE *out = fopen(path, "wb");
		creates.add(secondsSince(before));
		if (!out)
			break;
		for (int part = 0; part < 16; part++) {
			before = std::chrono::steady_clock::now();
			fwrite(block, 1, sizeof block, out);
			fflush(out);
			parts.add(secondsSince(before));
		}
		before = std::chrono::steady_clock::now();
		fclose(out);
		closes.add(secondsSince(before));
		for (int part = 0; part < 16; part++) {
			before = std::chrono::steady_clock::now();
			FILE *in = fopen(path, "rb");
			opens.add(secondsSince(before));
			if (!in)
				break;
			before = std::chrono::steady_clock::now();
			fseek(in, part * 65536L, SEEK_SET);
			fread(block, 1, sizeof block, in);
			reads.add(secondsSince(before));
			before = std::chrono::steady_clock::now();
			fclose(in);
			closes.add(secondsSince(before));
		}
	}
	for (int file = 0; file < 40; file++) {
		char path[96];
		snprintf(path, sizeof path, "/app0/rpcs3/probe-io/cach%04X.dat", file);
		unlink(path);
	}
	rmdir("/app0/rpcs3/probe-io");
	stop = true;
	ticker.join();
	trace(("probe-io: files: " + creates.text("creates") + "; " + parts.text("64 KiB writes") + "; " + opens.text("opens") + "; " +
		reads.text("reads") + "; " + closes.text("closes") + "; the process stood still up to " + std::to_string(int(stood * 1000)) + " ms").c_str());
}

} // namespace

/* How fast the title's CPUs run, and whether it can have more of them (build
 * 185, where /app0/rpcs3-probe-cpu.txt is): a title runs on 13 of the console's
 * 16 CPU threads, and GTA V keeps about eight threads busy on its six cores and
 * a half. Each clock is read from a chain of dependent adds, alone and with
 * every CPU the title has busy; then CPUs 13 to 15 are asked for, one at a
 * time, and the kernel's CPU mode */
extern "C" int32_t sceKernelGetCurrentCpu(void);
extern "C" int sceKernelGetCpumode(void);

namespace {

/* Billions of adds a second on the calling thread: eight dependent adds an
 * iteration, one cycle each, so about the clock in GHz */
double addChainGhz(uint64_t iterations)
{
	const auto start = std::chrono::steady_clock::now();
	uint64_t x = 1, n = iterations;
	__asm__ volatile(
		"1:\n\t"
		"add %0, %0\n\tadd %0, %0\n\tadd %0, %0\n\tadd %0, %0\n\t"
		"add %0, %0\n\tadd %0, %0\n\tadd %0, %0\n\tadd %0, %0\n\t"
		"dec %1\n\tjnz 1b"
		: "+r"(x), "+r"(n));
	const double seconds = secondsSince(start);
	return seconds > 0 ? iterations * 8.0 / seconds / 1e9 : 0;
}

std::string ghzText(int)
{
	char text[32];
	snprintf(text, sizeof text, " (%.2f GHz)", addChainGhz(200000000));
	return text;
}

bool pinTo(int cpu)
{
	const uint64_t mask = uint64_t{1} << cpu;
	if (libre_pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) != 0)
		return false;
	for (int tries = 0; tries < 1000 && sceKernelGetCurrentCpu() != cpu; tries++)
		std::this_thread::yield();
	return sceKernelGetCurrentCpu() == cpu;
}

void probeCpu()
{
	if (access("/app0/rpcs3-probe-cpu.txt", F_OK) != 0)
		return;
	uint64_t own = 0;
	libre_pthread_getaffinity_np(pthread_self(), sizeof(own), &own);
	char line[512];
	snprintf(line, sizeof line, "probe-cpu: the title's CPUs 0x%llx, the kernel's CPU mode %d", (unsigned long long)own, sceKernelGetCpumode());
	trace(line);

	/* Each CPU alone */
	std::string alone = "probe-cpu: GHz alone:";
	for (int cpu = 0; cpu < 16; cpu++) {
		if (!(own >> cpu & 1))
			continue;
		if (!pinTo(cpu)) {
			alone += " " + std::to_string(cpu) + "=not placed";
			continue;
		}
		addChainGhz(20000000); /* the clock rises first */
		snprintf(line, sizeof line, " %d=%.2f", cpu, addChainGhz(200000000));
		alone += line;
	}
	trace(alone.c_str());

	/* All at once */
	std::vector<double> busy(16, 0.0);
	std::vector<std::thread> threads;
	std::atomic<int> ready{0};
	for (int cpu = 0; cpu < 16; cpu++) {
		if (!(own >> cpu & 1))
			continue;
		threads.emplace_back([&, cpu] {
			if (!pinTo(cpu))
				return;
			ready++;
			while (ready.load() < __builtin_popcountll(own))
				std::this_thread::yield();
			addChainGhz(20000000);
			busy[cpu] = addChainGhz(200000000);
		});
	}
	for (auto &thread : threads)
		thread.join();
	std::string all = "probe-cpu: GHz with all busy:";
	for (int cpu = 0; cpu < 16; cpu++)
		if (own >> cpu & 1) {
			snprintf(line, sizeof line, " %d=%.2f", cpu, busy[cpu]);
			all += line;
		}
	trace(all.c_str());

	/* The three the title is not given */
	std::string more = "probe-cpu: asking for more:";
	for (int cpu = 13; cpu < 16; cpu++) {
		const uint64_t mask = uint64_t{1} << cpu;
		const int result = libre_pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);
		std::this_thread::yield();
		snprintf(line, sizeof line, " CPU %d: affinity %d, now on %d%s", cpu, result, sceKernelGetCurrentCpu(),
			result == 0 && sceKernelGetCurrentCpu() == cpu ? ghzText(cpu).c_str() : "");
		more += line;
	}
	libre_pthread_setaffinity_np(pthread_self(), sizeof(own), &own);
	trace(more.c_str());
}

} // namespace

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
	probeIo();
	probeCpu();
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
