/**
 * Dreamcast debug overlay + log.
 *
 * Wii64 parked strings in a GX HUD and optionally a USB Gecko. Here the
 * same DEBUG_print API fills a ring (RetroArch-style console) and, when
 * printToSD is on, appends to saves/not64.log. A failed open is remembered
 * so we do not retry fopen on every line.
 */

#include "dc_debug.h"
#include <stdio.h>
#include <string.h>

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

extern char *get_savespath(void);

static void log_open(void)
{
	char path[180];
	const char *dir;

	if (log_fp || log_failed || !printToSD)
		return;
	fileBrowser_kos_bind();
	if (saveFile_dir)
		fileBrowser_kos_init(saveFile_dir);
	dir = get_savespath();
	if (!dir)
		dir = "./saves";
	snprintf(path, sizeof(path), "%s/not64.log", dir);
	log_fp = fopen(path, "a");
	if (!log_fp)
		log_failed = 1;
}

void dc_debug_close(void)
{
	if (log_fp) {
		fclose(log_fp);
		log_fp = NULL;
	}
}

void dc_debug_reset(void)
{
	int i;

	dc_debug_close();
	log_failed = 0;
	log_seq = 0;
	log_head = 0;
	memset(stats_buffer, 0, sizeof(stats_buffer));
	memset(avge_counter, 0, sizeof(avge_counter));
	for (i = 0; i < DC_LOG_LINES; ++i)
		log_text[i][0] = '\0';
}

unsigned dc_debug_seq(void)
{
	return log_seq;
}

void DEBUG_print(char *string, int pos)
{
	unsigned row;
	size_t n;
	char copy[DC_LOG_WIDTH];

	if (!string)
		return;

	if (pos == DBG_SDGECKOOPEN) {
		log_open();
		return;
	}
	if (pos == DBG_SDGECKOCLOSE) {
		dc_debug_close();
		return;
	}

	n = strlen(string);
	if (printToScreen) {
		fputs(string, stdout);
		if (n == 0 || string[n - 1] != '\n')
			fputc('\n', stdout);
	}

	log_open();
	if (log_fp) {
		fprintf(log_fp, "%04u %s", log_seq, string);
		if (n == 0 || string[n - 1] != '\n')
			fputc('\n', log_fp);
		fflush(log_fp);
	}

	strncpy(copy, string, DC_LOG_WIDTH - 1);
	copy[DC_LOG_WIDTH - 1] = '\0';
	n = strlen(copy);
	if (n && (copy[n - 1] == '\n' || copy[n - 1] == '\r'))
		copy[n - 1] = '\0';

	if (pos >= 0 && pos < DC_LOG_LINES && pos != DBG_SDGECKOPRINT)
		row = (unsigned)pos;
	else
		row = log_head++ % DC_LOG_LINES;

	memset(log_text[row], 0, DC_LOG_WIDTH);
	strncpy(log_text[row], copy, DC_LOG_WIDTH - 1);
	log_seq++;
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
	snprintf(txtbuffer, sizeof(txtbuffer), "%s [ %u ]",
		 info ? info : "", value);
	DEBUG_print(txtbuffer, DBG_STATSBASE + stats_id);
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
int dc_debug_selftest(void)
{
	int fails = 0;
	char **rows;
	FILE *fp;
	char buf[128];
	char *dir;
	char path[180];
	extern char *get_savespath(void);

	dc_debug_reset();
	printToScreen = 0;
	printToSD = 1;
	DEBUG_print("ring-one\n", -1);
	DEBUG_print("ring-two", -1);
	rows = DEBUG_get_text();
	if (!rows || !strstr(rows[0], "ring-one") || !strstr(rows[1], "ring-two")) {
		printf("debug FAIL: ring\n");
		fails++;
	}
	if (dc_debug_seq() != 2) {
		printf("debug FAIL: seq\n");
		fails++;
	}
	dc_debug_close();
	dir = get_savespath();
	snprintf(path, sizeof(path), "%s/not64.log", dir ? dir : "./saves");
	fp = fopen(path, "r");
	if (!fp) {
		printf("debug FAIL: log open %s\n", path);
		fails++;
	} else {
		int saw = 0;
		while (fgets(buf, sizeof(buf), fp))
			if (strstr(buf, "ring-one"))
				saw = 1;
		fclose(fp);
		if (!saw) {
			printf("debug FAIL: log contents\n");
			fails++;
		}
	}
	printToSD = 0;
	printToScreen = 1;
	dc_debug_reset();
	printf("debug selftest: %s (%d failure(s))\n",
	       fails ? "FAIL" : "PASS", fails);
	return fails;
}
#endif
