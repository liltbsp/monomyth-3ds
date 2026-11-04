/* Monomyth 3DS dock: a touch panel on the bottom screen.
 *
 * Layout (320x240), modelled on a dual-screen menu style:
 *   Right column: eight menu tiles (MAP, POKEMON, BAG, ...) plus two small
 *                 shortcut tiles underneath. Each tile runs a "macro".
 *   Left panel:   tabs. TOOLS has emulator tools (speed, save states, screen
 *                 size, screenshot). BUTTONS has big on-screen GBA buttons.
 *   Footer:       build number and short status messages.
 *
 * A macro is a list of GBA button presses fed into the game one press at a
 * time, as if you pressed them yourself. No game code or data is touched.
 *
 * Macro tokens (comma separated):
 *   A B SELECT START UP DOWN LEFT RIGHT L R   press that button
 *   W<n>                                      wait n frames (W12)
 *   @<id>                                     move the START menu cursor to
 *                                             the entry whose action id is <id>
 *
 * "@" needs to know where the game keeps its START menu state in memory
 * (cursor_addr, actions_addr, count_addr in dock.ini). Until those are found,
 * the dock falls back to a guess: it assumes the vanilla Emerald menu order
 * and remembers where it last left the cursor.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "dock.h"
#include "ctr-gpu.h"

/* Build number, written by the GitHub workflow so you can tell builds apart. */
#if defined(__has_include)
#if __has_include("build-stamp.h")
#include "build-stamp.h"
#endif
#endif
#ifndef MONOMYTH_BUILD
#define MONOMYTH_BUILD "dev"
#endif

#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba-util/gui/font.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Theme. Colors are written as RGB and converted to the GPU's ABGR order.  */

#define RGB(r, g, b) (0xFF000000u | ((u32) (b) << 16) | ((u32) (g) << 8) | (u32) (r))

#define COL_FRAME       RGB(0x4A, 0x2C, 0x7A) /* right column background */
#define COL_PANEL       RGB(0xE8, 0xDE, 0xF7) /* left panel background */
#define COL_PANEL_LINE  RGB(0xC9, 0xB6, 0xEA)
#define COL_OUTLINE     RGB(0x3A, 0x2A, 0x55)
#define COL_TILE        RGB(0xFA, 0xF8, 0xFD)
#define COL_TILE_HI     RGB(0xC9, 0xA8, 0xF0) /* tapped tile */
#define COL_ACCENT      RGB(0x8E, 0x5B, 0xD6) /* active tab, toggles */
#define COL_TEXT        RGB(0x2A, 0x1E, 0x3D)
#define COL_TEXT_LIGHT  RGB(0xFF, 0xFF, 0xFF)
#define COL_VALUE       RGB(0x7B, 0x2F, 0xC4)
#define COL_ARROW       RGB(0x6E, 0x5A, 0x8C)
#define COL_FOOTER      RGB(0x3A, 0x2A, 0x55)
#define COL_DIM         0xB0FFFFFFu

/* ------------------------------------------------------------------------ */
/* Buttons and macros                                                       */

enum {
	K_A = 0, K_B, K_SELECT, K_START, K_RIGHT, K_LEFT, K_UP, K_DOWN, K_R, K_L
};

#define MENU_TILES 8
#define SHORT_TILES 2
#define MACRO_BUTTONS (MENU_TILES + SHORT_TILES)
#define PAD_BUTTONS 6
#define MAX_TOKENS 48
#define MAX_LABEL 16
#define MAX_MENU 12

#define PRESS_FRAMES 3
#define RELEASE_FRAMES 3

enum TokenType { T_KEY, T_WAIT, T_MENU };

struct Token {
	enum TokenType type;
	int value; /* key bit, frame count, or action id */
};

struct Macro {
	char label[MAX_LABEL];
	struct Token tokens[MAX_TOKENS];
	int nTokens;
	u32 icon; /* icon color for menu tiles */
};

struct Rect {
	s16 x, y, w, h;
};

/* Config buttons 1-8 are the menu tiles, 9-10 the shortcut tiles. */
static struct Macro macros[MACRO_BUTTONS];
static struct Macro pad[PAD_BUTTONS];
static struct Rect tileRects[MACRO_BUTTONS];
static struct Rect padRects[PAD_BUTTONS];

/* ------------------------------------------------------------------------ */
/* Left panel                                                               */

enum Tab { TAB_TOOLS, TAB_BUTTONS, TAB_MAX };
static const char* const tabNames[TAB_MAX] = { "TOOLS", "BUTTONS" };
static struct Rect tabRects[TAB_MAX];
static enum Tab activeTab = TAB_TOOLS;

enum Row { ROW_SPEED, ROW_FRAMESKIP, ROW_SLOT, ROW_SAVE, ROW_LOAD, ROW_SCREEN, ROW_SHOT, ROW_MAX };
static struct Rect rowRects[ROW_MAX];

static const char* const speedNames[] = { "1x", "2x", "3x", "4x", "MAX" };
static const int speedValues[] = { 1, 2, 3, 4, 0 };
#define SPEED_COUNT 5
static int speedIndex;

static const char* const frameskipNames[] = { "OFF", "1", "2", "3" };
#define FRAMESKIP_COUNT 4
static int frameskipIndex;

static const char* const screenNames[] = { "PIXEL", "FIT", "FULL" };
#define SCREEN_COUNT 3
static int screenIndex;
static int screenRequest = -1;

#define SLOT_MAX 9
static int stateSlot = 1;
static int loadArmFrames; /* LOAD needs a second tap while this is > 0 */

/* ------------------------------------------------------------------------ */
/* State                                                                    */

static struct GUIFont* dockFont;
static C3D_Tex whiteTex;
static bool whiteReady;

static u32 cursorAddr, actionsAddr, countAddr;
static const u8 fallbackMenu[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
static int guessedCursor;

static const struct Macro* running;
static int tokenIndex;
static int frameTimer;
static u16 heldNow;
static int pendingMoves; /* >0 = DOWN presses, <0 = UP presses */

/* Visual feedback: which thing was tapped, and for how long to light it. */
enum HitKind { HIT_NONE, HIT_TILE, HIT_PAD, HIT_TAB, HIT_ROW };
static enum HitKind litKind = HIT_NONE;
static int litIndex = -1;
static int litFrames;

static char toast[48];
static int toastFrames;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */

static int _ieq(const char* a, const char* b) {
	while (*a && *b) {
		if (toupper((unsigned char) *a) != toupper((unsigned char) *b)) {
			return 0;
		}
		++a;
		++b;
	}
	return *a == *b;
}

static void _toast(const char* text) {
	strncpy(toast, text, sizeof(toast) - 1);
	toast[sizeof(toast) - 1] = '\0';
	toastFrames = 150;
}

static bool _inRect(const struct Rect* r, u16 px, u16 py) {
	return px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h;
}

static int _keyFromName(const char* s) {
	static const struct { const char* name; int bit; } names[] = {
		{ "A", K_A }, { "B", K_B }, { "SELECT", K_SELECT }, { "START", K_START },
		{ "RIGHT", K_RIGHT }, { "LEFT", K_LEFT }, { "UP", K_UP }, { "DOWN", K_DOWN },
		{ "R", K_R }, { "L", K_L },
	};
	size_t i;
	for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		if (_ieq(s, names[i].name)) {
			return names[i].bit;
		}
	}
	return -1;
}

static void _parseMacro(struct Macro* m, const char* text) {
	char buf[256];
	strncpy(buf, text, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';
	m->nTokens = 0;
	char* cur = buf;
	while (*cur && m->nTokens < MAX_TOKENS) {
		while (*cur == ',' || *cur == ' ' || *cur == '\t') {
			++cur;
		}
		if (!*cur) {
			break;
		}
		char* tok = cur;
		while (*cur && *cur != ',' && *cur != ' ' && *cur != '\t') {
			++cur;
		}
		if (*cur) {
			*cur++ = '\0';
		}
		struct Token* t = &m->tokens[m->nTokens];
		if ((tok[0] == 'W' || tok[0] == 'w') && isdigit((unsigned char) tok[1])) {
			t->type = T_WAIT;
			t->value = atoi(&tok[1]);
		} else if (tok[0] == '@' && isdigit((unsigned char) tok[1])) {
			t->type = T_MENU;
			t->value = atoi(&tok[1]);
		} else {
			int key = _keyFromName(tok);
			if (key < 0) {
				continue;
			}
			t->type = T_KEY;
			t->value = key;
		}
		++m->nTokens;
	}
}

static void _setMacro(struct Macro* m, const char* label, const char* macro, u32 icon) {
	strncpy(m->label, label, MAX_LABEL - 1);
	m->label[MAX_LABEL - 1] = '\0';
	_parseMacro(m, macro);
	m->icon = icon;
}

/* ------------------------------------------------------------------------ */
/* Layout and defaults                                                      */

static void _layout(void) {
	int i;
	/* Right column: 8 tiles, then 2 small tiles. */
	for (i = 0; i < MENU_TILES; ++i) {
		tileRects[i] = (struct Rect) { 210, (s16) (5 + i * 24), 106, 22 };
	}
	tileRects[MENU_TILES + 0] = (struct Rect) { 210, 200, 51, 36 };
	tileRects[MENU_TILES + 1] = (struct Rect) { 265, 200, 51, 36 };

	/* Left panel tabs. */
	tabRects[TAB_TOOLS] = (struct Rect) { 8, 6, 92, 20 };
	tabRects[TAB_BUTTONS] = (struct Rect) { 104, 6, 92, 20 };

	/* TOOLS rows. */
	for (i = 0; i < ROW_MAX; ++i) {
		rowRects[i] = (struct Rect) { 8, (s16) (30 + i * 27), 188, 25 };
	}

	/* BUTTONS grid: 2 columns x 3 rows. */
	for (i = 0; i < PAD_BUTTONS; ++i) {
		padRects[i] = (struct Rect) { (s16) (8 + (i % 2) * 96), (s16) (32 + (i / 2) * 62), 92, 58 };
	}
}

static void _defaults(void) {
	/* Vanilla Emerald START menu action ids:
	 * 0 POKEDEX, 1 POKEMON, 2 BAG, 3 POKENAV, 4 PLAYER, 5 SAVE, 6 OPTION, 7 EXIT
	 * MAP opens the PokeNav, then its first entry (the region map). */
	_setMacro(&macros[0], "MAP", "START,W12,@3,A,W45,A", RGB(0xE8, 0xC2, 0x3A));
	_setMacro(&macros[1], "POK\xC3\xA9MON", "START,W12,@1,A", RGB(0xE0, 0x4A, 0x3C));
	_setMacro(&macros[2], "BAG", "START,W12,@2,A", RGB(0xD9, 0x8A, 0x2B));
	_setMacro(&macros[3], "CARD", "START,W12,@4,A", RGB(0x5A, 0x8C, 0xD8));
	_setMacro(&macros[4], "POK\xC3\xA9" "DEX", "START,W12,@0,A", RGB(0xD8, 0x3A, 0x5E));
	_setMacro(&macros[5], "POK\xC3\xA9NAV", "START,W12,@3,A", RGB(0xF0, 0x8A, 0x24));
	_setMacro(&macros[6], "SAVE", "START,W12,@5,A", RGB(0x6C, 0xB4, 0xE4));
	_setMacro(&macros[7], "OPTION", "START,W12,@6,A", RGB(0x8C, 0x8C, 0x9E));
	_setMacro(&macros[8], "ITEM", "SELECT", RGB(0x8E, 0x5B, 0xD6));
	_setMacro(&macros[9], "START", "START", RGB(0x8E, 0x5B, 0xD6));

	_setMacro(&pad[0], "A", "A", 0);
	_setMacro(&pad[1], "B", "B", 0);
	_setMacro(&pad[2], "L", "L", 0);
	_setMacro(&pad[3], "R", "R", 0);
	_setMacro(&pad[4], "START", "START", 0);
	_setMacro(&pad[5], "SELECT", "SELECT", 0);

	cursorAddr = actionsAddr = countAddr = 0;
}

static char* _trim(char* s) {
	while (isspace((unsigned char) *s)) {
		++s;
	}
	char* end = s + strlen(s);
	while (end > s && isspace((unsigned char) end[-1])) {
		--end;
	}
	*end = '\0';
	return s;
}

void dockLoadConfig(const char* path) {
	_defaults();
	FILE* f = fopen(path, "r");
	if (!f) {
		return;
	}
	char line[320];
	while (fgets(line, sizeof(line), f)) {
		char* s = _trim(line);
		if (!*s || *s == '#' || *s == ';' || *s == '[') {
			continue;
		}
		char* eq = strchr(s, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		char* key = _trim(s);
		char* value = _trim(eq + 1);
		int n;
		char field[16];
		if (sscanf(key, "button%d.%15s", &n, field) == 2 && n >= 1 && n <= MACRO_BUTTONS) {
			struct Macro* m = &macros[n - 1];
			if (_ieq(field, "label")) {
				strncpy(m->label, value, MAX_LABEL - 1);
				m->label[MAX_LABEL - 1] = '\0';
			} else if (_ieq(field, "macro")) {
				_parseMacro(m, value);
			}
		} else if (_ieq(key, "cursor_addr")) {
			cursorAddr = strtoul(value, NULL, 16);
		} else if (_ieq(key, "actions_addr")) {
			actionsAddr = strtoul(value, NULL, 16);
		} else if (_ieq(key, "count_addr")) {
			countAddr = strtoul(value, NULL, 16);
		}
	}
	fclose(f);
}

void dockInit(struct GUIFont* font) {
	dockFont = font;
	_layout();
	_defaults();
	if (C3D_TexInit(&whiteTex, 8, 8, GPU_RGBA8)) {
		memset(whiteTex.data, 0xFF, 8 * 8 * 4);
		GSPGPU_FlushDataCache(whiteTex.data, 8 * 8 * 4);
		whiteReady = true;
	}
	dockLoadConfig(DOCK_CONFIG_PATH);
}

void dockDeinit(void) {
	if (whiteReady) {
		C3D_TexDelete(&whiteTex);
		whiteReady = false;
	}
}

int dockSpeed(void) {
	return speedValues[speedIndex];
}

int dockTakeScreenRequest(void) {
	int r = screenRequest;
	screenRequest = -1;
	return r;
}

void dockSetScreenIndex(int index) {
	if (index >= 0 && index < SCREEN_COUNT && screenRequest < 0) {
		screenIndex = index;
	}
}

/* ------------------------------------------------------------------------ */
/* Macro runner                                                             */

static int _menuMoves(struct mCore* core, int actionId) {
	u8 menu[MAX_MENU];
	int count, cursor;
	if (cursorAddr && actionsAddr && countAddr) {
		count = core->busRead8(core, countAddr);
		if (count <= 0 || count > MAX_MENU) {
			return 0;
		}
		int i;
		for (i = 0; i < count; ++i) {
			menu[i] = core->busRead8(core, actionsAddr + i);
		}
		cursor = core->busRead8(core, cursorAddr);
	} else {
		count = sizeof(fallbackMenu);
		memcpy(menu, fallbackMenu, count);
		cursor = guessedCursor;
	}
	int target = -1;
	int i;
	for (i = 0; i < count; ++i) {
		if (menu[i] == actionId) {
			target = i;
			break;
		}
	}
	if (target < 0 || cursor < 0 || cursor >= count) {
		return 0;
	}
	guessedCursor = target;
	int down = (target - cursor + count) % count;
	int up = count - down;
	return down <= up ? down : -up;
}

static void _startMacro(const struct Macro* m) {
	if (!m->nTokens) {
		return;
	}
	running = m;
	tokenIndex = 0;
	frameTimer = 0;
	heldNow = 0;
	pendingMoves = 0;
}

static u16 _stepMacro(struct mCore* core) {
	if (frameTimer > 0) {
		--frameTimer;
		return heldNow;
	}
	if (heldNow) {
		heldNow = 0;
		frameTimer = RELEASE_FRAMES - 1;
		return 0;
	}
	if (pendingMoves) {
		heldNow = 1 << (pendingMoves > 0 ? K_DOWN : K_UP);
		pendingMoves += pendingMoves > 0 ? -1 : 1;
		frameTimer = PRESS_FRAMES - 1;
		return heldNow;
	}
	if (tokenIndex >= running->nTokens) {
		running = NULL;
		return 0;
	}
	const struct Token* t = &running->tokens[tokenIndex++];
	switch (t->type) {
	case T_KEY:
		heldNow = 1 << t->value;
		frameTimer = PRESS_FRAMES - 1;
		return heldNow;
	case T_WAIT:
		frameTimer = t->value > 0 ? t->value - 1 : 0;
		return 0;
	case T_MENU:
		pendingMoves = _menuMoves(core, t->value);
		return 0;
	}
	return 0;
}

/* ------------------------------------------------------------------------ */
/* Tools                                                                    */

static void _light(enum HitKind kind, int index) {
	litKind = kind;
	litIndex = index;
	litFrames = 10;
}

/* dir: -1 = left arrow, +1 = right arrow / centre. */
static void _rowTapped(struct mCore* core, int row, int dir) {
	char msg[48];
	switch (row) {
	case ROW_SPEED:
		speedIndex = (speedIndex + dir + SPEED_COUNT) % SPEED_COUNT;
		snprintf(msg, sizeof(msg), "Speed %s", speedNames[speedIndex]);
		_toast(msg);
		break;
	case ROW_FRAMESKIP:
		frameskipIndex = (frameskipIndex + dir + FRAMESKIP_COUNT) % FRAMESKIP_COUNT;
		mCoreConfigSetIntValue(&core->config, "frameskip", frameskipIndex);
		core->reloadConfigOption(core, "frameskip", NULL);
		snprintf(msg, sizeof(msg), "Frameskip %s", frameskipNames[frameskipIndex]);
		_toast(msg);
		break;
	case ROW_SLOT:
		stateSlot = (stateSlot - 1 + dir + SLOT_MAX) % SLOT_MAX + 1;
		loadArmFrames = 0;
		break;
	case ROW_SAVE:
		if (mCoreSaveState(core, stateSlot, SAVESTATE_SCREENSHOT | SAVESTATE_SAVEDATA | SAVESTATE_RTC | SAVESTATE_METADATA)) {
			snprintf(msg, sizeof(msg), "Saved state to slot %d", stateSlot);
		} else {
			snprintf(msg, sizeof(msg), "Couldn't save slot %d", stateSlot);
		}
		_toast(msg);
		break;
	case ROW_LOAD:
		if (loadArmFrames > 0) {
			loadArmFrames = 0;
			if (mCoreLoadState(core, stateSlot, SAVESTATE_SCREENSHOT | SAVESTATE_RTC)) {
				snprintf(msg, sizeof(msg), "Loaded slot %d", stateSlot);
			} else {
				snprintf(msg, sizeof(msg), "Slot %d is empty", stateSlot);
			}
		} else {
			loadArmFrames = 150;
			snprintf(msg, sizeof(msg), "Tap again to confirm");
		}
		_toast(msg);
		break;
	case ROW_SCREEN:
		screenIndex = (screenIndex + dir + SCREEN_COUNT) % SCREEN_COUNT;
		screenRequest = screenIndex;
		break;
	case ROW_SHOT:
		mCoreTakeScreenshot(core);
		_toast("Screenshot saved");
		break;
	default:
		break;
	}
}

static void _handleTap(struct mCore* core, u16 px, u16 py) {
	int i;
	if (!running) {
		for (i = 0; i < MACRO_BUTTONS; ++i) {
			if (_inRect(&tileRects[i], px, py)) {
				_light(HIT_TILE, i);
				_startMacro(&macros[i]);
				return;
			}
		}
	}
	for (i = 0; i < TAB_MAX; ++i) {
		if (_inRect(&tabRects[i], px, py)) {
			activeTab = (enum Tab) i;
			return;
		}
	}
	if (activeTab == TAB_TOOLS) {
		for (i = 0; i < ROW_MAX; ++i) {
			const struct Rect* r = &rowRects[i];
			if (_inRect(r, px, py)) {
				int dir = (px < r->x + r->w / 4) ? -1 : 1;
				_light(HIT_ROW, i);
				_rowTapped(core, i, dir);
				return;
			}
		}
	} else if (!running) {
		for (i = 0; i < PAD_BUTTONS; ++i) {
			if (_inRect(&padRects[i], px, py)) {
				_light(HIT_PAD, i);
				_startMacro(&pad[i]);
				return;
			}
		}
	}
}

uint16_t dockUpdate(struct mCore* core, u32 held, u32 down, bool active) {
	/* mGBA scans input before we do, which uses up hidKeysDown()'s "just
	 * pressed" signal for the touch screen. So detect taps ourselves:
	 * touching now, but not on the previous frame. */
	static bool wasTouching = false;
	UNUSED(down);
	bool touching = (held & KEY_TOUCH) != 0;
	bool tapped = touching && !wasTouching;
	wasTouching = touching;
	if (core->opts.frameskip >= 0 && core->opts.frameskip < FRAMESKIP_COUNT) {
		frameskipIndex = core->opts.frameskip;
	}
	if (litFrames > 0) {
		--litFrames;
	}
	if (toastFrames > 0) {
		--toastFrames;
	}
	if (loadArmFrames > 0) {
		--loadArmFrames;
	}
	if (!active) {
		running = NULL;
		heldNow = 0;
		return 0;
	}
	if (tapped) {
		touchPosition pos;
		hidTouchRead(&pos);
		_handleTap(core, pos.px, pos.py);
	}
	if (!running) {
		return 0;
	}
	if (litKind == HIT_TILE || litKind == HIT_PAD) {
		litFrames = 10; /* keep the tile lit while its macro plays */
	}
	return _stepMacro(core);
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                  */

static void _rect(u32 color, s16 x, s16 y, s16 w, s16 h) {
	if (w > 0 && h > 0) {
		ctrAddRectEx(color, x, y, w, h, 0, 0, 8, 8, 0);
	}
}

/* A box with clipped corners: drawn as two overlapping rectangles. */
static void _chamfer(u32 color, s16 x, s16 y, s16 w, s16 h, s16 c) {
	_rect(color, x + c, y, w - 2 * c, h);
	_rect(color, x, y + c, w, h - 2 * c);
}

static void _box(const struct Rect* r, u32 fill, u32 outline) {
	_chamfer(outline, r->x, r->y, r->w, r->h, 3);
	_chamfer(fill, r->x + 1, r->y + 1, r->w - 2, r->h - 2, 2);
}

static bool _isLit(enum HitKind kind, int index) {
	return litFrames > 0 && litKind == kind && litIndex == index;
}

static void _text(int x, int y, enum GUIAlignment align, u32 color, const char* s) {
	GUIFontPrint(dockFont, x, y, align, color, s);
}

void dockDraw(void) {
	if (!whiteReady || !dockFont) {
		return;
	}
	int fh = (int) GUIFontHeight(dockFont);
	int i;
	char buf[48];

	/* ---- Shapes ---- */
	ctrActivateTexture(&whiteTex);
	_rect(COL_FRAME, 0, 0, 320, 240);
	_chamfer(COL_PANEL, 2, 2, 202, 218, 4);
	_rect(COL_FOOTER, 2, 222, 202, 16);

	/* Menu tiles. */
	for (i = 0; i < MACRO_BUTTONS; ++i) {
		const struct Rect* r = &tileRects[i];
		_box(r, _isLit(HIT_TILE, i) ? COL_TILE_HI : COL_TILE, COL_OUTLINE);
		if (i < MENU_TILES) {
			/* Icon chip on the left of the tile. */
			_chamfer(COL_OUTLINE, r->x + 4, r->y + 4, 16, 14, 2);
			_chamfer(macros[i].icon, r->x + 5, r->y + 5, 14, 12, 2);
		}
	}

	/* Tabs. */
	for (i = 0; i < TAB_MAX; ++i) {
		bool on = activeTab == (enum Tab) i;
		_box(&tabRects[i], on ? COL_ACCENT : COL_TILE, COL_OUTLINE);
	}

	if (activeTab == TAB_TOOLS) {
		for (i = 0; i < ROW_MAX; ++i) {
			bool lit = _isLit(HIT_ROW, i);
			bool armed = i == ROW_LOAD && loadArmFrames > 0;
			_box(&rowRects[i], (lit || armed) ? COL_TILE_HI : COL_TILE, COL_OUTLINE);
		}
	} else {
		for (i = 0; i < PAD_BUTTONS; ++i) {
			_box(&padRects[i], _isLit(HIT_PAD, i) ? COL_TILE_HI : COL_TILE, COL_OUTLINE);
		}
	}
	ctrFlushBatch();

	/* ---- Text ---- */
	for (i = 0; i < MACRO_BUTTONS; ++i) {
		const struct Rect* r = &tileRects[i];
		if (i < MENU_TILES) {
			_text(r->x + 64, r->y + (r->h + fh) / 2 - 2, GUI_ALIGN_HCENTER, COL_TEXT, macros[i].label);
		} else {
			_text(r->x + r->w / 2, r->y + (r->h + fh) / 2 - 2, GUI_ALIGN_HCENTER, COL_TEXT, macros[i].label);
		}
	}

	for (i = 0; i < TAB_MAX; ++i) {
		const struct Rect* r = &tabRects[i];
		bool on = activeTab == (enum Tab) i;
		_text(r->x + r->w / 2, r->y + (r->h + fh) / 2 - 2, GUI_ALIGN_HCENTER, on ? COL_TEXT_LIGHT : COL_TEXT, tabNames[i]);
	}

	if (activeTab == TAB_TOOLS) {
		for (i = 0; i < ROW_MAX; ++i) {
			const struct Rect* r = &rowRects[i];
			int cx = r->x + r->w / 2;
			int ty = r->y + (r->h + fh) / 2 - 2;
			const char* label = "";
			const char* value = NULL;
			switch (i) {
			case ROW_SPEED:
				label = "SPEED";
				value = speedNames[speedIndex];
				break;
			case ROW_FRAMESKIP:
				label = "FRAMESKIP";
				value = frameskipNames[frameskipIndex];
				break;
			case ROW_SLOT:
				label = "STATE SLOT";
				snprintf(buf, sizeof(buf), "%d", stateSlot);
				value = buf;
				break;
			case ROW_SAVE:
				label = "SAVE STATE";
				break;
			case ROW_LOAD:
				label = loadArmFrames > 0 ? "TAP AGAIN TO LOAD" : "LOAD STATE";
				break;
			case ROW_SCREEN:
				label = "SCREEN";
				value = screenNames[screenIndex];
				break;
			case ROW_SHOT:
				label = "SCREENSHOT";
				break;
			}
			if (value) {
				/* "LABEL      < value >" on one line, like a settings list. */
				_text(r->x + 8, ty, GUI_ALIGN_LEFT, COL_TEXT, label);
				_text(r->x + r->w - 46, ty, GUI_ALIGN_HCENTER, COL_VALUE, value);
				_text(r->x + r->w - 84, ty, GUI_ALIGN_LEFT, COL_ARROW, "<");
				_text(r->x + r->w - 8, ty, GUI_ALIGN_RIGHT, COL_ARROW, ">");
			} else {
				_text(cx, ty, GUI_ALIGN_HCENTER, COL_TEXT, label);
			}
		}
	} else {
		for (i = 0; i < PAD_BUTTONS; ++i) {
			const struct Rect* r = &padRects[i];
			_text(r->x + r->w / 2, r->y + (r->h + fh) / 2 - 2, GUI_ALIGN_HCENTER, COL_TEXT, pad[i].label);
		}
	}

	/* Footer: status message, or build number and menu mode. */
	int fy = 222 + (16 + fh) / 2 - 3;
	if (toastFrames > 0) {
		_text(103, fy, GUI_ALIGN_HCENTER, COL_TEXT_LIGHT, toast);
	} else {
		snprintf(buf, sizeof(buf), "build %s", MONOMYTH_BUILD);
		_text(8, fy, GUI_ALIGN_LEFT, COL_DIM, buf);
		_text(198, fy, GUI_ALIGN_RIGHT, COL_DIM, (cursorAddr && actionsAddr && countAddr) ? "menu: live" : "menu: guess");
	}
	ctrFlushBatch();
}
