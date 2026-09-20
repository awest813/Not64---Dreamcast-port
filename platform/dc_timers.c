#include "../main/winlnxdefs.h"
#include "../main/rom.h"
#include "../main/timers.h"
#include <stdio.h>

#ifdef DC_HOST_STUB
#include <time.h>
static unsigned int host_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned int)(ts.tv_sec * 1000000u + ts.tv_nsec / 1000u);
}
#define ticks_to_microsecs(x) (x)
#define gettick() host_us()
#else
#include <kos.h>
#define ticks_to_microsecs(x) (x)
static unsigned int gettick(void)
{
	return (unsigned int)(timer_ms_gettime64() * 1000ull);
}
#endif

timers Timers = {0.0f, 0.0f, 0, 1, 0, 100};
float VILimit = 60.0f;
double VILimitMicroseconds = 1000000.0/60.0;

float GetVILimit(void)
{
	switch (ROM_HEADER.Country_code & 0xFF) {
	case 0x44: case 0x46: case 0x49: case 0x50:
	case 0x53: case 0x55: case 0x58: case 0x59:
		return 50.0f;
	default:
		return 60.0f;
	}
}

void InitTimer(void)
{
	VILimit = GetVILimit();
	VILimitMicroseconds = 1000000.0 / VILimit;
	Timers.frameDrawn = 0;
    Timers.vis = 0;
}

extern int stop;

void new_frame(void)
{
	(void)gettick();
	Timers.frameDrawn = 1;
}

void new_vi(void)
{
	Timers.vis += 1.0f;
}
