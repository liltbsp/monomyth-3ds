/* Monomyth 3DS dock: a touch panel on the bottom screen.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef MONOMYTH_DOCK_H
#define MONOMYTH_DOCK_H

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>

struct mCore;
struct GUIFont;

#define DOCK_CONFIG_PATH "sdmc:/3ds/monomyth-3ds/dock.ini"

void dockInit(struct GUIFont* font);
void dockDeinit(void);
void dockLoadConfig(const char* path);

/* Call once per emulated frame from pollGameInput.
 * held: 3DS keys from hidKeysHeld(); down: hidKeysDown().
 * Returns GBA key bits the dock wants pressed this frame. */
uint16_t dockUpdate(struct mCore* core, u32 held, u32 down, bool active);

/* Draw the dock. The bottom screen must be the current render target
 * with a 320x240 viewport. */
void dockDraw(void);

/* Emulation speed chosen on the dock: 1-4 = that many times normal speed,
 * 0 = unlimited. */
int dockSpeed(void);

/* Screen size picked on the dock: returns 0 (pixel), 1 (fit) or 2 (stretch)
 * once after the player changes it, otherwise -1. */
int dockTakeScreenRequest(void);

/* Tell the dock which screen size is active, so it shows the right value. */
void dockSetScreenIndex(int index);

#endif
