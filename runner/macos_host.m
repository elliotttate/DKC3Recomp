#import "macos_host.h"

#import <AppKit/AppKit.h>

#include <mach/mach_time.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum Dkc3MacMenuItemIndex {
  kDkc3MacItemToggleOverlay,
  kDkc3MacItemQuickSave,
  kDkc3MacItemQuickLoad,
  kDkc3MacItemFullscreen,
  kDkc3MacItemNearest,
  kDkc3MacItemBilinear,
  kDkc3MacItemAspectNative,
  kDkc3MacItemAspect16x10,
  kDkc3MacItemAspect16x9,
  kDkc3MacItemAspect21x9,
  kDkc3MacItemCount,
} Dkc3MacMenuItemIndex;

@interface Dkc3MacMenuController : NSObject
- (void)runCommand:(id)sender;
@end

static Dkc3MacMenuController *s_menu_controller;
static NSMenuItem *s_menu_items[kDkc3MacItemCount];
static _Atomic uint32_t s_pending_commands;

@implementation Dkc3MacMenuController
- (void)runCommand:(id)sender {
  atomic_fetch_or(&s_pending_commands, (uint32_t)[sender tag]);
}
@end

static void SetError(char *error, size_t capacity, NSString *message) {
  if (!error || capacity == 0)
    return;
  const char *text = message.UTF8String;
  (void)snprintf(error, capacity, "%s", text ? text : "macOS error");
}

static NSString *ExecutableDirectory(void) {
  NSString *path = [NSBundle mainBundle].executablePath;
  return path.length ? path.stringByDeletingLastPathComponent : nil;
}

static NSString *ExistingAssetsPath(NSString *original_directory) {
  NSFileManager *manager = [NSFileManager defaultManager];
  const char *override = getenv("DKC3_ASSETS_PATH");
  if (override && *override) {
    NSString *path = [manager stringWithFileSystemRepresentation:override
                                                          length:strlen(override)];
    if ([manager fileExistsAtPath:path])
      return path;
  }
  NSString *executable_assets =
      [ExecutableDirectory() stringByAppendingPathComponent:@"assets"];
  if ([manager fileExistsAtPath:executable_assets])
    return executable_assets;
  NSString *working_assets =
      [original_directory stringByAppendingPathComponent:@"assets"];
  if ([manager fileExistsAtPath:working_assets])
    return working_assets;
  return executable_assets ? executable_assets : working_assets;
}

bool Dkc3MacPrepareRuntimeDirectory(char *assets_path,
                                    size_t assets_capacity,
                                    char *error,
                                    size_t error_capacity) {
  @autoreleasepool {
    NSFileManager *manager = [NSFileManager defaultManager];
    NSString *original_directory = manager.currentDirectoryPath;
    NSString *assets = ExistingAssetsPath(original_directory);
    NSString *runtime = nil;
    const char *explicit_directory = getenv("DKC3_USER_DIR");
    if (explicit_directory && *explicit_directory) {
      runtime = [manager
          stringWithFileSystemRepresentation:explicit_directory
                                      length:strlen(explicit_directory)];
    } else if (getenv("DKC3_PORTABLE") &&
               strcmp(getenv("DKC3_PORTABLE"), "0") != 0) {
      runtime = ExecutableDirectory();
    } else {
      NSURL *support = [[manager URLsForDirectory:NSApplicationSupportDirectory
                                        inDomains:NSUserDomainMask] firstObject];
      runtime = [[[support URLByAppendingPathComponent:@"Flat2VR"
                                            isDirectory:YES]
          URLByAppendingPathComponent:@"DKC3Recomp" isDirectory:YES] path];
    }
    if (!runtime.length) {
      SetError(error, error_capacity,
               @"Unable to resolve the DKC3 application data directory.");
      return false;
    }
    NSError *directory_error = nil;
    if (![manager createDirectoryAtPath:runtime
            withIntermediateDirectories:YES attributes:nil
                                 error:&directory_error]) {
      SetError(error, error_capacity, directory_error.localizedDescription);
      return false;
    }
    if (chdir(runtime.fileSystemRepresentation) != 0) {
      SetError(error, error_capacity,
               @"Unable to enter the DKC3 application data directory.");
      return false;
    }
    if (assets_path && assets_capacity) {
      const char *asset_bytes = assets.fileSystemRepresentation;
      if (!asset_bytes || strlen(asset_bytes) >= assets_capacity) {
        SetError(error, error_capacity,
                 @"The DKC3 launcher asset path is too long.");
        return false;
      }
      (void)snprintf(assets_path, assets_capacity, "%s", asset_bytes);
    }
    fprintf(stdout, "User data: %s\n", runtime.fileSystemRepresentation);
    return true;
  }
}

static NSMenuItem *AddCommand(NSMenu *menu, NSString *title,
                              uint32_t command, NSString *key,
                              NSEventModifierFlags modifiers,
                              Dkc3MacMenuItemIndex item_index) {
  NSMenuItem *item = [[NSMenuItem alloc]
      initWithTitle:title
             action:@selector(runCommand:)
      keyEquivalent:key ? key : @""];
  item.target = s_menu_controller;
  item.tag = command;
  if (key.length)
    item.keyEquivalentModifierMask = modifiers;
  [menu addItem:item];
  if (item_index >= 0 && item_index < kDkc3MacItemCount)
    s_menu_items[item_index] = item;
  return item;
}

static void AddSubmenu(NSMenu *parent, NSString *title, NSMenu *submenu) {
  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title
                                                action:nil
                                         keyEquivalent:@""];
  item.submenu = submenu;
  [parent addItem:item];
}

void Dkc3MacInstallMenu(void) {
  @autoreleasepool {
    [NSApplication sharedApplication];
    if (s_menu_controller)
      return;
    s_menu_controller = [[Dkc3MacMenuController alloc] init];
    NSMenu *bar = [[NSMenu alloc] initWithTitle:@""];

    NSMenu *app = [[NSMenu alloc] initWithTitle:@"DKC3Recomp"];
    NSMenuItem *about = [[NSMenuItem alloc]
        initWithTitle:@"About DKC3Recomp"
               action:@selector(orderFrontStandardAboutPanel:)
        keyEquivalent:@""];
    about.target = NSApp;
    [app addItem:about];
    [app addItem:[NSMenuItem separatorItem]];
    NSMenuItem *hide = [[NSMenuItem alloc]
        initWithTitle:@"Hide DKC3Recomp"
               action:@selector(hide:)
        keyEquivalent:@"h"];
    hide.target = NSApp;
    [app addItem:hide];
    NSMenuItem *hide_others = [[NSMenuItem alloc]
        initWithTitle:@"Hide Others"
               action:@selector(hideOtherApplications:)
        keyEquivalent:@"h"];
    hide_others.target = NSApp;
    hide_others.keyEquivalentModifierMask =
        NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [app addItem:hide_others];
    NSMenuItem *show_all = [[NSMenuItem alloc]
        initWithTitle:@"Show All"
               action:@selector(unhideAllApplications:)
        keyEquivalent:@""];
    show_all.target = NSApp;
    [app addItem:show_all];
    [app addItem:[NSMenuItem separatorItem]];
    AddCommand(app, @"Quit DKC3Recomp", kDkc3MacCommandQuit, @"q",
               NSEventModifierFlagCommand, kDkc3MacItemCount);
    AddSubmenu(bar, @"DKC3Recomp", app);

    NSMenu *game = [[NSMenu alloc] initWithTitle:@"Game"];
    AddCommand(game, @"Pause / Settings", kDkc3MacCommandToggleOverlay,
               @",", NSEventModifierFlagCommand,
               kDkc3MacItemToggleOverlay);
    [game addItem:[NSMenuItem separatorItem]];
    AddCommand(game, @"Quick Save State", kDkc3MacCommandQuickSave, @"s",
               NSEventModifierFlagCommand, kDkc3MacItemQuickSave);
    AddCommand(game, @"Quick Load State", kDkc3MacCommandQuickLoad, @"l",
               NSEventModifierFlagCommand, kDkc3MacItemQuickLoad);
    AddSubmenu(bar, @"Game", game);

    NSMenu *view = [[NSMenu alloc] initWithTitle:@"View"];
    AddCommand(view, @"Enter Full Screen", kDkc3MacCommandToggleFullscreen,
               @"f", NSEventModifierFlagControl | NSEventModifierFlagCommand,
               kDkc3MacItemFullscreen);
    NSMenu *scaling = [[NSMenu alloc] initWithTitle:@"Scaling"];
    AddCommand(scaling, @"Pixel Sharp (Nearest)",
               kDkc3MacCommandFilterNearest, @"", 0,
               kDkc3MacItemNearest);
    AddCommand(scaling, @"Smooth (Bilinear)",
               kDkc3MacCommandFilterBilinear, @"", 0,
               kDkc3MacItemBilinear);
    AddSubmenu(view, @"Scaling", scaling);
    NSMenu *aspect = [[NSMenu alloc] initWithTitle:@"Aspect Ratio"];
    AddCommand(aspect, @"Native 4:3 (256x224)",
               kDkc3MacCommandAspectNative, @"", 0,
               kDkc3MacItemAspectNative);
    AddCommand(aspect, @"Widescreen 16:10 (308x224)",
               kDkc3MacCommandAspect16x10, @"", 0,
               kDkc3MacItemAspect16x10);
    AddCommand(aspect, @"Widescreen 16:9 (342x224)",
               kDkc3MacCommandAspect16x9, @"", 0,
               kDkc3MacItemAspect16x9);
    AddCommand(aspect, @"Ultrawide 21:9 (446x224)",
               kDkc3MacCommandAspect21x9, @"", 0,
               kDkc3MacItemAspect21x9);
    AddSubmenu(view, @"Aspect Ratio", aspect);
    AddSubmenu(bar, @"View", view);

    NSApp.mainMenu = bar;
  }
}

uint32_t Dkc3MacTakeCommands(void) {
  return atomic_exchange(&s_pending_commands, 0);
}

void Dkc3MacUpdateMenu(bool fullscreen, bool linear_filter, int aspect) {
  @autoreleasepool {
    s_menu_items[kDkc3MacItemFullscreen].title =
        fullscreen ? @"Exit Full Screen" : @"Enter Full Screen";
    s_menu_items[kDkc3MacItemNearest].state =
        linear_filter ? NSControlStateValueOff : NSControlStateValueOn;
    s_menu_items[kDkc3MacItemBilinear].state =
        linear_filter ? NSControlStateValueOn : NSControlStateValueOff;
    s_menu_items[kDkc3MacItemAspectNative].state =
        aspect == 0 ? NSControlStateValueOn : NSControlStateValueOff;
    s_menu_items[kDkc3MacItemAspect16x10].state =
        aspect == 1 ? NSControlStateValueOn : NSControlStateValueOff;
    s_menu_items[kDkc3MacItemAspect16x9].state =
        aspect == 2 ? NSControlStateValueOn : NSControlStateValueOff;
    s_menu_items[kDkc3MacItemAspect21x9].state =
        aspect == 3 ? NSControlStateValueOn : NSControlStateValueOff;
    s_menu_items[kDkc3MacItemQuickSave].enabled = YES;
    s_menu_items[kDkc3MacItemQuickLoad].enabled = YES;
  }
}

void Dkc3MacWaitSeconds(double seconds) {
  if (seconds <= 0.0)
    return;
  static mach_timebase_info_data_t timebase;
  if (!timebase.denom)
    (void)mach_timebase_info(&timebase);
  const double absolute_per_second =
      1000000000.0 * (double)timebase.denom / (double)timebase.numer;
  const uint64_t interval = (uint64_t)(seconds * absolute_per_second);
  if (!interval)
    return;
  const uint64_t target = mach_absolute_time() + interval;
  const uint64_t spin = (uint64_t)(absolute_per_second * 0.0015);
  if (interval > spin)
    (void)mach_wait_until(target - spin);
  while (mach_absolute_time() < target) {
#if defined(__aarch64__) || defined(__arm64__)
    __asm__ volatile("yield");
#elif defined(__x86_64__)
    __asm__ volatile("pause");
#endif
  }
}
