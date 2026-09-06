#ifndef DKC3_WINDOWS_MENU_H
#define DKC3_WINDOWS_MENU_H

#include "desktop_menu.h"

typedef struct Dkc3WindowsMenu Dkc3WindowsMenu;
void Dkc3WindowsWatchTitlebars(bool enabled);
Dkc3WindowsMenu *Dkc3WindowsMenuCreate(void *sdl_window);
void Dkc3WindowsMenuDestroy(Dkc3WindowsMenu *menu);
unsigned Dkc3WindowsMenuEvent(Dkc3WindowsMenu *menu, const void *sdl_event);
void Dkc3WindowsMenuUpdate(Dkc3WindowsMenu *menu, const Dkc3MenuState *state);

#endif
