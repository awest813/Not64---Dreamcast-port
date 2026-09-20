/**
 * Dreamcast debug overlay + session log.
 *
 * Modelled on mupen64plus DebugMessage / RetroArch's log file: one printf-
 * style call, a ring for an overlay, and an append-only session file.
 * Wii64 split HUD (printToScreen) from SD (printToSD); we keep that split.
 * Overlay off still records ERROR to stdout so a failed test is visible.
 */

#include "dc_debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef DC_HOST_STUB
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#else
#include <kos.h>
#endif

#include "../fileBrowser/fileBrowser.h"
#include "../fileBrowser/fileBrowser-kos.h"

char txtbuffer[1024];
char printToSD;
extern char printToScreen;

#define DC_LOG_LINES DEBUG_TEXT_HEIGHT
#define DC_LOG_WIDTH DEBUG_TEXT_WIDTH

static char log_text[DC_LOG_LINES][DC_LOG_WIDTH];
static char *log_ptrs[DC_LOG_LINES];
static unsigned log_seq;
static unsigned log_head;
static unsigned stats_buffer[20];
static unsigned avge_counter[20];
static FILE *log_fp;
static int log_failed;
static int session_written;
static char last_line[DC_LOG_WIDTH];

extern char *get_savespath(void);

static unsigned now_ms(void)
{
#ifdef DC_HOST_STUB
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (unsigned)(ts.tv_sec * 1000u + (unsigned)(ts.tv_nsec / 1000000u));
#else
	return (unsigned)timer_ms_gettime64();
#endif
}

const char *dc_debug_log_path(void)
{
	static char path[180];
	const char *dir = get_savespath();

	if (!dir)
		dir = "./saves";
	snprintf(path, sizeof(path), "%s/not64.log", dir);
	return path;
}

static void log_open(void)
{
	if (log_fp || log_failed || !printToSD)
		return;
	fileBrowser_kos_bind();
	if (saveFile_dir)
		fileBrowser_kos_init(saveFile_dir);
	log_fp = fopen(dc_debug_log_path(), "a");
	if (!log_fp) {
		log_failed = 1;
		return;
	}
	if (!session_written) {
		fprintf(log_fp, "----- not64-dc log t=%u -----\n", now_ms());
		fflush(log_fp);
		session_written = 1;
	}
}

void dc_debug_close(void)
{
	if (log_fp) {
		fflush(log_fp);
		fclose(log_fp);
		log_fp = NULL;
	}
	/* Next open is a new session (gecko close, end of run, selftest). */
	session_written = 0;
}

void dc_debug_reset(void)
{
	int i;

	dc_debug_close();
	log_failed = 0;
	log_seq = 0;
	log_head = 0;
	session_written = 0;
	last_line[0] = '\0';
	memset(stats_buffer, 0, sizeof(stats_buffer));
	memset(avge_counter, 0, sizeof(avge_counter));
	for (i = 0; i < DC_LOG_LINES; ++i)
		log_text[i][0] = '\0';
}

void dc_debug_set_file(int on)
{
	printToSD = on ? 1 : 0;
	if (!on)
		dc_debug_close();
	else
		log_failed = 0;
}

unsigned dc_debug_seq(void)
{
	return log_seq;
}

int dc_debug_last(char *buf, size_t n)
{
	if (!buf || n == 0)
		return -1;
	strncpy(buf, last_line, n - 1);
	buf[n - 1] = '\0';
	return last_line[0] ? 0 : -1;
}

static void strip_eol(char *s)
{
	size_t n = strlen(s);

	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r'))
		s[--n] = '\0';
}

static void ring_store(const char *s, int pos)
{
	unsigned row;
	char copy[DC_LOG_WIDTH];

	strncpy(copy, s, DC_LOG_WIDTH - 1);
	copy[DC_LOG_WIDTH - 1] = '\0';
	strip_eol(copy);

	strncpy(last_line, copy, DC_LOG_WIDTH - 1);
	last_line[DC_LOG_WIDTH - 1] = '\0';

	/* Gecko control codes are not HUD rows. */
	if (pos >= 0 && pos < DC_LOG_LINES)
		row = (unsigned)pos;
	else
		row = log_head++ % DC_LOG_LINES;
	memset(log_text[row], 0, DC_LOG_WIDTH);
	strncpy(log_text[row], copy, DC_LOG_WIDTH - 1);
}

static void emit(int level, const char *string, int pos, int to_ring)
{
	char copy[256];
	int echo;

	if (!string)
		return;
	strncpy(copy, string, sizeof(copy) - 1);
	copy[sizeof(copy) - 1] = '\0';
	strip_eol(copy);

	echo = printToScreen || (level == DC_LOG_ERROR);
	if (echo) {
		fputs(copy, stdout);
		fputc('\n', stdout);
	}

	log_open();
	if (log_fp) {
		fprintf(log_fp, "%u t=%u %s %s\n", log_seq, now_ms(),
			level == DC_LOG_ERROR ? "ERR" : "INF", copy);
		fflush(log_fp);
	}

	if (to_ring)
		ring_store(copy, pos);
	else {
		strncpy(last_line, copy, DC_LOG_WIDTH - 1);
		last_line[DC_LOG_WIDTH - 1] = '\0';
	}
	log_seq++;
}

void DEBUG_print(char *string, int pos)
{
	if (!string)
		return;

	if (pos == DBG_SDGECKOOPEN) {
		log_failed = 0;
		log_open();
		return;
	}
	if (pos == DBG_SDGECKOCLOSE) {
		dc_debug_close();
		return;
	}
	/* Wii SD gecko print was file-only; keep that split. */
	if (pos == DBG_SDGECKOPRINT) {
		emit(DC_LOG_INFO, string, pos, 0);
		return;
	}

	emit(DC_LOG_INFO, string, pos, 1);
}

void dc_log(int level, const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	if (!fmt)
		return;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	emit(level, buf, -1, 1);
}

void DEBUG_stats(int stats_id, char *info, unsigned int stats_type,
		 unsigned int adjustment_value)
{
	unsigned value;

	if (stats_id < 0 || stats_id >= 20)
		return;
	switch (stats_type) {
	case STAT_TYPE_ACCUM:
		stats_buffer[stats_id] += adjustment_value;
		break;
	case STAT_TYPE_AVGE:
		avge_counter[stats_id] += 1;
		stats_buffer[stats_id] += adjustment_value;
		break;
	case STAT_TYPE_CLEAR:
		avge_counter[stats_id] = 0;
		stats_buffer[stats_id] = 0;
		break;
	default:
		break;
	}
	value = stats_buffer[stats_id];
	if (stats_type == STAT_TYPE_AVGE && avge_counter[stats_id])
		value /= avge_counter[stats_id];
	dc_log(DC_LOG_INFO, "%s [ %u ]", info ? info : "", value);
}

void DEBUG_update(void)
{
}

char **DEBUG_get_text(void)
{
	int i;
	for (i = 0; i < DC_LOG_LINES; ++i)
		log_ptrs[i] = log_text[i];
	return log_ptrs;
}

#ifdef DC_HOST_STUB
static int log_has(const char *needle)
{
	FILE *fp;
	char buf[256];
	int saw = 0;

	fp = fopen(dc_debug_log_path(), "r");
	if (!fp)
		return 0;
	while (fgets(buf, sizeof(buf), fp))
		if (strstr(buf, needle))
			saw = 1;
	fclose(fp);
	return saw;
}

int dc_debug_selftest(void)
{
	int fails = 0, i;
	char last[DC_LOG_WIDTH];
	char token[40], boom[64], gecko[80], leaked[80];
	char **rows;
	int saved_screen = printToScreen;
	int saved_sd = printToSD;
	int saved_out, cap_fd;
	const char *cap_path = "/tmp/not64-debug-cap.txt";
	char capbuf[128];

	snprintf(token, sizeof(token), "tok%u-%ld", dc_debug_seq(), (long)getpid());
	dc_debug_reset();
	printToScreen = 0;
	dc_debug_set_file(1);

	dc_log(DC_LOG_INFO, "ring-one %s", token);
	dc_log(DC_LOG_INFO, "ring-two %s", token);
	if (dc_debug_last(last, sizeof(last)) != 0 || !strstr(last, "ring-two")) {
		printf("debug FAIL: last line\n");
		fails++;
	}
	if (dc_debug_seq() != 2) {
		printf("debug FAIL: seq\n");
		fails++;
	}
	rows = DEBUG_get_text();
	if (!rows || !strstr(rows[0], "ring-one") || !strstr(rows[1], "ring-two")) {
		printf("debug FAIL: ring rows\n");
		fails++;
	}

	for (i = 0; i < DC_LOG_LINES + 5; ++i)
		dc_log(DC_LOG_INFO, "wrap-%d", i);

	{
		char want[32];
		int saw_old = 0;

		snprintf(want, sizeof(want), "wrap-%d", DC_LOG_LINES + 4);
		if (dc_debug_last(last, sizeof(last)) != 0 || !strstr(last, want)) {
			printf("debug FAIL: wrap last want %s got %s\n",
			       want, last);
			fails++;
		}
		rows = DEBUG_get_text();
		for (i = 0; i < DC_LOG_LINES; ++i)
			if (rows && !strcmp(rows[i], "wrap-0"))
				saw_old = 1;
		if (saw_old) {
			printf("debug FAIL: wrap-0 still in ring\n");
			fails++;
		}
	}

	snprintf(boom, sizeof(boom), "boom-%s", token);
	fflush(stdout);
	saved_out = dup(STDOUT_FILENO);
	cap_fd = open(cap_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (saved_out >= 0 && cap_fd >= 0) {
		dup2(cap_fd, STDOUT_FILENO);
		dc_log(DC_LOG_ERROR, "%s", boom);
		fflush(stdout);
		dup2(saved_out, STDOUT_FILENO);
		close(saved_out);
		lseek(cap_fd, 0, SEEK_SET);
		memset(capbuf, 0, sizeof(capbuf));
		if (read(cap_fd, capbuf, sizeof(capbuf) - 1) <= 0 ||
		    !strstr(capbuf, boom)) {
			printf("debug FAIL: ERROR not echoed to stdout\n");
			fails++;
		}
		close(cap_fd);
		unlink(cap_path);
	} else {
		dc_log(DC_LOG_ERROR, "%s", boom);
		if (saved_out >= 0)
			close(saved_out);
		if (cap_fd >= 0) {
			close(cap_fd);
			unlink(cap_path);
		}
	}

	snprintf(gecko, sizeof(gecko), "gecko-file-only %s", token);
	DEBUG_print(gecko, DBG_SDGECKOPRINT);
	dc_debug_close();

	if (!log_has(token) || !log_has(boom) || !log_has(gecko)) {
		printf("debug FAIL: session file missing token/err/gecko\n");
		fails++;
	}
	rows = DEBUG_get_text();
	if (rows) {
		for (i = 0; i < DC_LOG_LINES; ++i) {
			if (strstr(rows[i], "gecko-file-only")) {
				printf("debug FAIL: gecko print leaked into ring\n");
				fails++;
				break;
			}
		}
	}

	printToScreen = 0;
	dc_debug_set_file(0);
	snprintf(leaked, sizeof(leaked), "should-not-open-file %s", token);
	dc_log(DC_LOG_INFO, "%s", leaked);
	dc_debug_close();
	if (log_has(leaked)) {
		printf("debug FAIL: INFO wrote file while logfile Off\n");
		fails++;
	}

	printToScreen = saved_screen;
	dc_debug_set_file(saved_sd);
	dc_debug_reset();
	printf("debug selftest: %s (%d failure(s))\n",
	       fails ? "FAIL" : "PASS", fails);
	return fails;
}
#endif
