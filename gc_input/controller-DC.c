/**
 * Dreamcast Maple controller as a controller_t backend.
 */

#include <string.h>
#include "controller.h"

#ifndef DC_HOST_STUB
#include <kos.h>
#endif

enum {
	STICK_AS_ANALOG = 1,
	DPAD_AS_ANALOG = 2,
};

/*
 * A retail Dreamcast pad has no C, Z or D buttons and no second D-pad, so the
 * N64's Z, L, R and four C-buttons have nowhere to sit. The two analog
 * triggers and two shift gestures cover them:
 *
 *   left trigger              -> N64 Z
 *   right trigger             -> N64 R
 *   Y + left trigger          -> N64 L
 *   both triggers + D-pad     -> N64 C-buttons
 *
 * The triggers are analog, so they are folded into the Maple button word as
 * virtual bits and mapped through the ordinary button_t table. Bits 24+ are
 * well clear of every CONT_* KallistiOS defines (highest is bit 15).
 */
#define DC_VB_LTRIG     (1u << 24)  /* left trigger, unshifted  -> Z  */
#define DC_VB_RTRIG     (1u << 25)  /* right trigger            -> R  */
#define DC_VB_LTRIG_ALT (1u << 26)  /* left trigger while Y held -> L */

/* Maple triggers report 0-255. Past roughly a fifth of travel counts as a
 * press, which keeps a resting finger from latching a shift. */
#define DC_TRIG_THRESHOLD 48

#ifdef DC_HOST_STUB
#define CONT_C          (1u << 0)
#define CONT_B          (1u << 1)
#define CONT_A          (1u << 2)
#define CONT_START      (1u << 3)
#define CONT_DPAD_UP    (1u << 4)
#define CONT_DPAD_DOWN  (1u << 5)
#define CONT_DPAD_LEFT  (1u << 6)
#define CONT_DPAD_RIGHT (1u << 7)
#define CONT_Z          (1u << 8)
#define CONT_Y          (1u << 9)
#define CONT_X          (1u << 10)
#define CONT_D          (1u << 11)
#define CONT_DPAD2_UP   (1u << 12)
#define CONT_DPAD2_DOWN (1u << 13)
#define CONT_DPAD2_LEFT (1u << 14)
#define CONT_DPAD2_RIGHT (1u << 15)
#endif

static button_t buttons[] = {
	{  0, ~0u,             "None" },
	{  1, CONT_DPAD_UP,    "D-Up" },
	{  2, CONT_DPAD_LEFT,  "D-Left" },
	{  3, CONT_DPAD_RIGHT, "D-Right" },
	{  4, CONT_DPAD_DOWN,  "D-Down" },
	{  5, CONT_A,          "A" },
	{  6, CONT_B,          "B" },
	{  7, CONT_X,          "X" },
	{  8, CONT_Y,          "Y" },
	{  9, CONT_START,      "Start" },
	{ 10, CONT_C,          "C" },
	{ 11, CONT_Z,          "Z" },
	{ 12, CONT_D,          "D" },
	{ 13, CONT_DPAD2_UP,   "C-Up" },
	{ 14, CONT_DPAD2_LEFT, "C-Left" },
	{ 15, CONT_DPAD2_RIGHT,"C-Right" },
	{ 16, CONT_DPAD2_DOWN, "C-Down" },
	{ 17, DC_VB_LTRIG,     "L-Trigger" },
	{ 18, DC_VB_RTRIG,     "R-Trigger" },
	{ 19, DC_VB_LTRIG_ALT, "Y+L-Trigger" },
};

static button_t analog_sources[] = {
	{ 0, STICK_AS_ANALOG, "Analog Stick" },
	{ 1, DPAD_AS_ANALOG,  "D-Pad" },
};

static button_t menu_combos[] = {
	{ 0, CONT_START | CONT_A | CONT_B, "Start+A+B" },
	{ 1, CONT_START | CONT_X,          "Start+X" },
};

static unsigned int last_buttons[4];
static int last_joyx[4];
static int last_joyy[4];
#ifdef DC_HOST_STUB
static unsigned int host_buttons[4];
static int host_joyx[4] = {128, 128, 128, 128};
static int host_joyy[4] = {128, 128, 128, 128};
static int host_ltrig[4];
static int host_rtrig[4];

void controller_DC_host_set(int Control, unsigned int buttons, int jx, int jy)
{
	if (Control < 0 || Control > 3)
		return;
	host_buttons[Control] = buttons;
	host_joyx[Control] = jx;
	host_joyy[Control] = jy;
}

void controller_DC_host_set_triggers(int Control, int ltrig, int rtrig)
{
	if (Control < 0 || Control > 3)
		return;
	host_ltrig[Control] = ltrig;
	host_rtrig[Control] = rtrig;
}
#endif

static int poll_pad(int Control, unsigned int *buttons_out, int *jx, int *jy,
		    int *ltrig, int *rtrig)
{
#ifdef DC_HOST_STUB
	*buttons_out = host_buttons[Control];
	*jx = host_joyx[Control];
	*jy = host_joyy[Control];
	*ltrig = host_ltrig[Control];
	*rtrig = host_rtrig[Control];
	return 1;
#else
	maple_device_t *dev;
	cont_state_t *st;

	dev = maple_enum_type(Control, MAPLE_FUNC_CONTROLLER);
	if (!dev)
		return 0;
	st = (cont_state_t *)maple_dev_status(dev);
	if (!st)
		return 0;
	*buttons_out = (unsigned int)st->buttons;
	*jx = st->joyx;
	*jy = st->joyy;
	*ltrig = st->ltrig;
	*rtrig = st->rtrig;
	return 1;
#endif
}

#define DC_DPAD_MASK \
	(CONT_DPAD_UP | CONT_DPAD_DOWN | CONT_DPAD_LEFT | CONT_DPAD_RIGHT)

/*
 * Fold the analog triggers into the button word and apply the two shifts.
 * Everything downstream then works through the ordinary button_t masks.
 */
static unsigned int dc_virtual_buttons(unsigned int b, int ltrig, int rtrig)
{
	int lheld = ltrig >= DC_TRIG_THRESHOLD;
	int rheld = rtrig >= DC_TRIG_THRESHOLD;

	if (lheld && rheld) {
		/* Both triggers: the D-pad becomes the C-buttons. The triggers'
		 * own bindings are withheld for the duration, so reaching for a
		 * C-button does not also mash Z and R. */
		if (b & CONT_DPAD_UP)
			b |= CONT_DPAD2_UP;
		if (b & CONT_DPAD_DOWN)
			b |= CONT_DPAD2_DOWN;
		if (b & CONT_DPAD_LEFT)
			b |= CONT_DPAD2_LEFT;
		if (b & CONT_DPAD_RIGHT)
			b |= CONT_DPAD2_RIGHT;
		return b & ~DC_DPAD_MASK;
	}

	if (lheld)
		b |= (b & CONT_Y) ? DC_VB_LTRIG_ALT : DC_VB_LTRIG;
	if (rheld)
		b |= DC_VB_RTRIG;
	return b;
}

static int _GetKeys(int Control, BUTTONS *Keys, controller_config_t *config)
{
	BUTTONS *c = Keys;
	unsigned int b;
	int jx, jy, ltrig = 0, rtrig = 0;

	memset(c, 0, sizeof(BUTTONS));
	if (!poll_pad(Control, &b, &jx, &jy, &ltrig, &rtrig))
		return 0;

	b = dc_virtual_buttons(b, ltrig, rtrig);

	/* Post-shift, so callers see what the mapping actually saw. */
	last_buttons[Control] = b;
	last_joyx[Control] = jx;
	last_joyy[Control] = jy;

	{
		int isHeld_mask;
#define isHeld(button) (((button)->mask == ~0u) ? 0 : ((b & (button)->mask) == (button)->mask))
		c->R_DPAD       = isHeld(config->DR);
		c->L_DPAD       = isHeld(config->DL);
		c->D_DPAD       = isHeld(config->DD);
		c->U_DPAD       = isHeld(config->DU);
		c->START_BUTTON = isHeld(config->START);
		c->B_BUTTON     = isHeld(config->B);
		c->A_BUTTON     = isHeld(config->A);
		c->Z_TRIG       = isHeld(config->Z);
		c->R_TRIG       = isHeld(config->R);
		c->L_TRIG       = isHeld(config->L);
		c->R_CBUTTON    = isHeld(config->CR);
		c->L_CBUTTON    = isHeld(config->CL);
		c->D_CBUTTON    = isHeld(config->CD);
		c->U_CBUTTON    = isHeld(config->CU);
		isHeld_mask = isHeld(config->exit);
#undef isHeld

		if (config->analog->mask == STICK_AS_ANALOG) {
			/* Maple joy is 0–255 with 128 center; N64 axis is signed. */
			c->X_AXIS = (signed char)(jx - 128);
			c->Y_AXIS = (signed char)(128 - jy);
		} else if (config->analog->mask == DPAD_AS_ANALOG) {
			if (b & CONT_DPAD_RIGHT)
				c->X_AXIS = +80;
			else if (b & CONT_DPAD_LEFT)
				c->X_AXIS = -80;
			if (b & CONT_DPAD_UP)
				c->Y_AXIS = +80;
			else if (b & CONT_DPAD_DOWN)
				c->Y_AXIS = -80;
		}
		if (config->inverted & 2)
			c->X_AXIS = -c->X_AXIS;
		if (config->inverted & 1)
			c->Y_AXIS = -c->Y_AXIS;
		return isHeld_mask;
	}
}

static void pause_ctl(int Control)
{
	(void)Control;
}

static void resume_ctl(int Control)
{
	(void)Control;
}

static void rumble_ctl(int Control, int rumble)
{
	(void)Control;
	(void)rumble;
}

static void configure(int Control, controller_config_t *config)
{
	(void)Control;
	(void)config;
}

static void assign(int p, int v)
{
	(void)p;
	(void)v;
}

static void refreshAvailable(void);

controller_t controller_DC = {
	'D',
	_GetKeys,
	configure,
	assign,
	pause_ctl,
	resume_ctl,
	rumble_ctl,
	refreshAvailable,
	{0, 0, 0, 0},
	sizeof(buttons) / sizeof(buttons[0]),
	buttons,
	sizeof(analog_sources) / sizeof(analog_sources[0]),
	analog_sources,
	sizeof(menu_combos) / sizeof(menu_combos[0]),
	menu_combos,
	{
		.DU       = &buttons[1],
		.DL       = &buttons[2],
		.DR       = &buttons[3],
		.DD       = &buttons[4],
		.Z        = &buttons[17],   /* left trigger              */
		.L        = &buttons[19],   /* Y + left trigger          */
		.R        = &buttons[18],   /* right trigger             */
		.A        = &buttons[5],
		.B        = &buttons[6],
		.START    = &buttons[9],
		/* Reached by holding both triggers; see dc_virtual_buttons(). */
		.CU       = &buttons[13],
		.CL       = &buttons[14],
		.CR       = &buttons[15],
		.CD       = &buttons[16],
		.analog   = &analog_sources[0],
		.exit     = &menu_combos[0],
		.inverted = 0,
	}
};

static void refreshAvailable(void)
{
	int i;
	for (i = 0; i < 4; ++i) {
#ifdef DC_HOST_STUB
		controller_DC.available[i] = (i == 0);
#else
		controller_DC.available[i] =
			maple_enum_type(i, MAPLE_FUNC_CONTROLLER) ? 1 : 0;
#endif
	}
}

unsigned int controller_DC_lastButtons(int Control)
{
	if (Control < 0 || Control > 3)
		return 0;
	return last_buttons[Control];
}
