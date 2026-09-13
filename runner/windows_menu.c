#include "windows_menu.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

typedef struct MenuItem {
  wchar_t text[128];
  bool top, submenu;
} MenuItem;

struct Dkc3WindowsMenu {
  HWND window;
  HMENU bar;
  Dkc3MenuState previous;
  bool has_previous;
  HBRUSH background;
  HFONT font;
  MenuItem items[64];
  unsigned item_count;
};

static const COLORREF background_color = RGB(28, 29, 33);
static const COLORREF foreground_color = RGB(235, 235, 240);

static void ThemeTitlebar(HWND window) {
  /* Attribute 20 is Windows 10; optional caption/text colors need Windows 11. */
  BOOL dark = TRUE;
  DwmSetWindowAttribute(window, 20, &dark, sizeof dark);
  DwmSetWindowAttribute(window, 35, &background_color, sizeof background_color);
  DwmSetWindowAttribute(window, 36, &foreground_color, sizeof foreground_color);
}

static int SDLCALL TitlebarEvent(void *data, SDL_Event *event) {
  (void)data;
  if (event->type == SDL_WINDOWEVENT &&
      (event->window.event == SDL_WINDOWEVENT_SHOWN ||
       event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED)) {
    SDL_Window *window = SDL_GetWindowFromID(event->window.windowID);
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (window && SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_WINDOWS)
      ThemeTitlebar(info.info.win.window);
  }
  return 1;
}

void Dkc3WindowsWatchTitlebars(bool enabled) {
  if (enabled) SDL_AddEventWatch(TitlebarEvent, NULL);
  else SDL_DelEventWatch(TitlebarEvent, NULL);
}

/* Owner drawing uses documented Win32 APIs; no private uxtheme ordinals. */
static LRESULT CALLBACK MenuProc(HWND window, UINT message, WPARAM wparam,
                                LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
  Dkc3WindowsMenu *menu = (Dkc3WindowsMenu *)data;
  (void)id;
  if (message == WM_MEASUREITEM && wparam == 0) {
    MEASUREITEMSTRUCT *measure = (MEASUREITEMSTRUCT *)lparam;
    if (measure->CtlType == ODT_MENU && measure->itemData) {
      MenuItem *item = (MenuItem *)measure->itemData;
      HDC dc = GetDC(window);
      HGDIOBJ previous = SelectObject(dc, menu->font);
      SIZE size = {0};
      GetTextExtentPoint32W(dc, item->text, (int)wcslen(item->text), &size);
      measure->itemWidth = (UINT)(size.cx + (item->top ? 12 : 64));
      measure->itemHeight = (UINT)(size.cy + 10);
      SelectObject(dc, previous);
      ReleaseDC(window, dc);
      return TRUE;
    }
  } else if (message == WM_DRAWITEM && wparam == 0) {
    DRAWITEMSTRUCT *draw = (DRAWITEMSTRUCT *)lparam;
    if (draw->CtlType == ODT_MENU && draw->itemData) {
      MenuItem *item = (MenuItem *)draw->itemData;
      int saved = SaveDC(draw->hDC);
      SetDCBrushColor(draw->hDC, (draw->itemState & (ODS_SELECTED | ODS_HOTLIGHT))
          ? RGB(64, 66, 76) : background_color);
      FillRect(draw->hDC, &draw->rcItem, (HBRUSH)GetStockObject(DC_BRUSH));
      SetBkMode(draw->hDC, TRANSPARENT);
      SetTextColor(draw->hDC, (draw->itemState & ODS_DISABLED)
          ? RGB(135, 135, 145) : foreground_color);
      SelectObject(draw->hDC, menu->font);
      RECT text = draw->rcItem;
      text.left += item->top ? 6 : 28;
      text.right -= item->top ? 6 : 24;
      wchar_t label[128];
      wcscpy_s(label, 128, item->text);
      wchar_t *shortcut = wcschr(label, L'\t');
      if (shortcut) *shortcut++ = L'\0';
      UINT flags = DT_SINGLELINE | DT_VCENTER;
      if (draw->itemState & ODS_NOACCEL) flags |= DT_HIDEPREFIX;
      DrawTextW(draw->hDC, label, -1, &text, flags);
      if (shortcut) DrawTextW(draw->hDC, shortcut, -1, &text, flags | DT_RIGHT);
      if (draw->itemState & ODS_CHECKED) {
        RECT mark = draw->rcItem;
        mark.right = mark.left + 24;
        DrawTextW(draw->hDC, L"\x2713", 1, &mark, flags | DT_CENTER);
      }
      if (item->submenu && !item->top) {
        RECT arrow = draw->rcItem;
        arrow.left = arrow.right - 20;
        DrawTextW(draw->hDC, L"\x203a", 1, &arrow, flags | DT_CENTER);
      }
      RestoreDC(draw->hDC, saved);
      return TRUE;
    }
  } else if (message == WM_MENUCHAR) {
    HMENU popup = (HMENU)lparam;
    for (int i = 0; i < GetMenuItemCount(popup); ++i) {
      MENUITEMINFOW info = {sizeof info};
      info.fMask = MIIM_DATA;
      if (GetMenuItemInfoW(popup, (UINT)i, TRUE, &info) && info.dwItemData) {
        MenuItem *item = (MenuItem *)info.dwItemData;
        const wchar_t *key = wcschr(item->text, L'&');
        if (key && towupper(key[1]) == towupper((wchar_t)LOWORD(wparam)))
          return MAKELRESULT(i, MNC_EXECUTE);
      }
    }
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

static bool ThemeMenu(Dkc3WindowsMenu *menu, HMENU native, bool top) {
  MENUINFO background = {sizeof background};
  background.fMask = MIM_BACKGROUND;
  background.hbrBack = menu->background;
  if (!SetMenuInfo(native, &background)) return false;
  for (int i = 0; i < GetMenuItemCount(native); ++i) {
    if (menu->item_count >= 64) return false;
    MenuItem *item = &menu->items[menu->item_count++];
    HMENU popup = GetSubMenu(native, i);
    item->top = top;
    item->submenu = popup != NULL;
    if (!GetMenuStringW(native, (UINT)i, item->text, 128, MF_BYPOSITION)) return false;
    MENUITEMINFOW info = {sizeof info};
    info.fMask = MIIM_FTYPE | MIIM_DATA;
    info.fType = MFT_OWNERDRAW;
    info.dwItemData = (ULONG_PTR)item;
    if (!SetMenuItemInfoW(native, (UINT)i, TRUE, &info)) return false;
    if (popup && !ThemeMenu(menu, popup, false)) return false;
  }
  return true;
}

static bool Add(HMENU menu, unsigned id, const wchar_t *label) {
  return AppendMenuW(menu, MF_STRING, id, label) != 0;
}

static bool Choices(HMENU parent, const wchar_t *title, unsigned first,
                    const wchar_t *const *labels, unsigned count) {
  HMENU menu = CreatePopupMenu();
  if (!menu) return false;
  for (unsigned i = 0; i < count; ++i) {
    if (!Add(menu, first + i, labels[i])) {
      DestroyMenu(menu); return false;
    }
  }
  if (!AppendMenuW(parent, MF_POPUP, (UINT_PTR)menu, title)) {
    DestroyMenu(menu); return false;
  }
  return true;
}

Dkc3WindowsMenu *Dkc3WindowsMenuCreate(void *sdl_window) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!SDL_GetWindowWMInfo((SDL_Window *)sdl_window, &info) ||
      info.subsystem != SDL_SYSWM_WINDOWS) return NULL;
  Dkc3WindowsMenu *menu = calloc(1, sizeof *menu);
  if (!menu) return NULL;
  menu->window = info.info.win.window;
  menu->background = CreateSolidBrush(background_color);
  NONCLIENTMETRICSW metrics = {sizeof metrics};
  if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof metrics, &metrics, 0))
    menu->font = CreateFontIndirectW(&metrics.lfMenuFont);
  if (!menu->background || !menu->font ||
      !SetWindowSubclass(menu->window, MenuProc, 1, (DWORD_PTR)menu)) {
    Dkc3WindowsMenuDestroy(menu); return NULL;
  }
  ThemeTitlebar(menu->window);
  menu->bar = CreateMenu();
  HMENU game = CreatePopupMenu(), view = CreatePopupMenu();
  if (!menu->bar || !game || !view) {
    if (game) DestroyMenu(game);
    if (view) DestroyMenu(view);
    Dkc3WindowsMenuDestroy(menu); return NULL;
  }
  if (!AppendMenuW(menu->bar, MF_POPUP, (UINT_PTR)game, L"&Game")) {
    DestroyMenu(game); DestroyMenu(view);
    Dkc3WindowsMenuDestroy(menu); return NULL;
  }
  if (!AppendMenuW(menu->bar, MF_POPUP, (UINT_PTR)view, L"&View")) {
    DestroyMenu(view); Dkc3WindowsMenuDestroy(menu); return NULL;
  }
  static const wchar_t *const aspects[] = {
    L"Native 4:3 (256x224)", L"Widescreen 16:10 (308x224)",
    L"Widescreen 16:9 (342x224)", L"Ultrawide 21:9 (446x224)"};
  static const wchar_t *const scalers[] = {
    L"Pixel Sharp (Nearest)", L"Smooth (Bilinear)", L"Reconstruct (experimental)"};
  static const wchar_t *const modes[] = {
    L"Sharp pixels only", L"+ Dither decoding", L"+ Diagonal edges",
    L"+ Level-2 slopes", L"+ Level-3 slopes"};
  static const wchar_t *const edges[] = {
    L"Reflect terrain past the wall", L"Black past the wall",
    L"Shift view inward at the wall", L"Glide view inward at the wall"};
  static const wchar_t *const screens[] = {L"Raw", L"CRT", L"Composite", L"Trinitron"};
  bool ok = Add(game, kDkc3MenuPause, L"&Pause / Settings\tEsc") &&
      Add(game, kDkc3MenuSave, L"Quick &Save State") &&
      Add(game, kDkc3MenuLoad, L"Quick &Load State") &&
      Add(game, kDkc3MenuAbout, L"&About DKC3Recomp") &&
      Add(game, kDkc3MenuQuit, L"&Quit\tAlt+F4") &&
      Add(view, kDkc3MenuFullscreen, L"Toggle &Full Screen") &&
      Choices(view, L"&Aspect Ratio", kDkc3MenuAspect, aspects, 4) &&
      Choices(view, L"&Scaling", kDkc3MenuUpscaler, scalers, 3) &&
      Choices(view, L"&Reconstruction Mode", kDkc3MenuReconstruct, modes, 5) &&
      Choices(view, L"&Level Edge", kDkc3MenuEdge, edges, 4) &&
      Choices(view, L"Screen &Model", kDkc3MenuScreen, screens, 4);
  /* Attaching a bar keeps the outer frame and takes its height from the
   * client area SDL just sized, so restore the requested client size. On a
   * fullscreen window SDL only records the windowed size for later. */
  bool fullscreen = (SDL_GetWindowFlags((SDL_Window *)sdl_window) &
      (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
  int client_width = 0, client_height = 0;
  SDL_GetWindowSize((SDL_Window *)sdl_window, &client_width, &client_height);
  if (!ok || !ThemeMenu(menu, menu->bar, true) || !SetMenu(menu->window, menu->bar)) {
    Dkc3WindowsMenuDestroy(menu); return NULL;
  }
  DrawMenuBar(menu->window);
  if (!fullscreen && client_width > 0 && client_height > 0)
    SDL_SetWindowSize((SDL_Window *)sdl_window, client_width, client_height);
  SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
  return menu;
}

void Dkc3WindowsMenuSetVisible(Dkc3WindowsMenu *menu, bool visible) {
  if (!menu || !menu->bar || !IsWindow(menu->window)) return;
  if (visible == (GetMenu(menu->window) == menu->bar)) return;
  SetMenu(menu->window, visible ? menu->bar : NULL);
  DrawMenuBar(menu->window);
}

bool Dkc3WindowsMenuIsVisible(const Dkc3WindowsMenu *menu) {
  return menu && menu->bar && IsWindow(menu->window) &&
      GetMenu(menu->window) == menu->bar;
}

void Dkc3WindowsMenuDestroy(Dkc3WindowsMenu *menu) {
  if (!menu) return;
  RemoveWindowSubclass(menu->window, MenuProc, 1);
  if (menu->bar) {
    if (IsWindow(menu->window) && GetMenu(menu->window) == menu->bar)
      SetMenu(menu->window, NULL);
    DestroyMenu(menu->bar);
  }
  if (menu->font) DeleteObject(menu->font);
  if (menu->background) DeleteObject(menu->background);
  free(menu);
}

unsigned Dkc3WindowsMenuEvent(Dkc3WindowsMenu *menu, const void *sdl_event) {
  const SDL_Event *event = sdl_event;
  if (!menu || !event || event->type != SDL_SYSWMEVENT ||
      !event->syswm.msg || event->syswm.msg->subsystem != SDL_SYSWM_WINDOWS)
    return 0;
  const SDL_SysWMmsg *msg = event->syswm.msg;
  if (msg->msg.win.hwnd != menu->window || msg->msg.win.msg != WM_COMMAND ||
      msg->msg.win.lParam != 0 || HIWORD(msg->msg.win.wParam) != 0) return 0;
  unsigned id = LOWORD(msg->msg.win.wParam);
  return GetMenuState(menu->bar, id, MF_BYCOMMAND) != (UINT)-1 ? id : 0;
}

void Dkc3WindowsMenuUpdate(Dkc3WindowsMenu *menu, const Dkc3MenuState *state) {
  if (!menu || !state) return;
  const Dkc3MenuState *old = &menu->previous;
  if (menu->has_previous && old->aspect == state->aspect &&
      old->upscaler == state->upscaler && old->reconstruct == state->reconstruct &&
      old->edge == state->edge && old->screen == state->screen &&
      old->fullscreen == state->fullscreen) return;
  static const unsigned starts[] = {kDkc3MenuAspect, kDkc3MenuUpscaler,
    kDkc3MenuReconstruct, kDkc3MenuEdge, kDkc3MenuScreen};
  static const unsigned counts[] = {4, 3, 5, 4, 4};
  for (unsigned group = 0; group < 5; ++group)
    for (unsigned i = 0; i < counts[group]; ++i) {
      unsigned id = starts[group] + i;
      CheckMenuItem(menu->bar, id, MF_BYCOMMAND |
          (Dkc3MenuSelected(state, id) ? MF_CHECKED : MF_UNCHECKED));
    }
  MENUITEMINFOW info = {sizeof info};
  info.fMask = MIIM_DATA;
  if (GetMenuItemInfoW(menu->bar, kDkc3MenuFullscreen, FALSE, &info) && info.dwItemData) {
    MenuItem *item = (MenuItem *)info.dwItemData;
    wcscpy_s(item->text, 128, state->fullscreen ? L"Exit &Full Screen" : L"Enter &Full Screen");
    info.fMask = MIIM_STRING;
    info.dwTypeData = item->text;
    SetMenuItemInfoW(menu->bar, kDkc3MenuFullscreen, FALSE, &info);
  }
  menu->previous = *state;
  menu->has_previous = true;
  DrawMenuBar(menu->window);
}
