#include "../gui/DEBUG.h"
#include <stdio.h>
#include <string.h>

char txtbuffer[1024];
char printToSD;

void DEBUG_print(char* string, int pos)
{
	(void)pos;
	if (string)
		fputs(string, stdout);
}

void DEBUG_stats(int stats_id, char *info, unsigned int stats_type, unsigned int adjustment_value)
{
	(void)stats_id;
	(void)info;
	(void)stats_type;
	(void)adjustment_value;
}

void DEBUG_update(void)
{
}

char** DEBUG_get_text(void)
{
	return NULL;
}
