/**
 * Dreamcast debug overlay + log.
 *
 * Wii64's DEBUG.c parked strings in a GX HUD and optionally a USB Gecko.
 * Here we keep a small ring (RetroArch-style console) and, when printToSD
 * is on, append the same lines to saves/not64.log so a host run can be
 * grepped after the fact.
 */

#include "../gui/DEBUG.h"
#include <stdio.h>
#include <string.h>

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

extern char *get_savespath(void);

static void log_open(void)
{
	char path[180];
	const char *dir;

	if (log_fp || !printToSD)
		return;
	dir = get_savespath();
	if (!dir)
		dir = "./saves";
	snprintf(path, sizeof(path), "%s/not64.log", dir);
	log_fp = fopen(path, "a");
}

void DEBUG_print(char *string, int pos)
{
	unsigned row;
	size_t n;

	if (!string)
		return;

	if (pos == DBG_SDGECKOOPEN) {
		log_open();
		return;
	}
	if (pos == DBG_SDGECKOCLOSE) {
		if (log_fp) {
			fclose(log_fp);
			log_fp = NULL;
		}
		return;
	}

	n = strlen(string);
	if (printToScreen)
		fputs(string, stdout);

	log_open();
	if (log_fp) {
		fprintf(log_fp, "%04u %s", log_seq, string);
		if (n == 0 || string[n - 1] != '\n')
			fputc('\n', log_fp);
		fflush(log_fp);
	}

	if (pos >= 0 && pos < DC_LOG_LINES)
		row = (unsigned)pos;
	else
		row = log_head++ % DC_LOG_LINES;

	memset(log_text[row], 0, DC_LOG_WIDTH);
	strncpy(log_text[row], string, DC_LOG_WIDTH - 1);
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

unsigned dc_debug_seq(void)
{
	return log_seq;
}
