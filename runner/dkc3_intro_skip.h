#ifndef DKC3_INTRO_SKIP_H
#define DKC3_INTRO_SKIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opening sequence addresses for DKC3 USA (En,Fr), established from bank $80
 * and bank $B2 disassembly:
 *
 * - $004E..$0050: 24-bit main-loop vector.
 *   - $B2:8751 (State 0): Rare rolling/bouncing log & "Nintendo presents".
 *   - $B2:8B2C (State 1): Bouncing title / developer credit intro.
 *   - $B2:8EE5 (State 3): Copyright / legal screen before the Title Screen.
 *   - $B2:8E05 (State 2): Title Screen (already skippable natively).
 * - $06AB: 16-bit intro flags. On cold boot $80:82B4 clears it to 0, so
 *   $B2:8788 and $B2:8B76 skip controller polling. Setting bits 0 and 2
 *   ($0005, the same state the cartridge sets after the first attract loop)
 *   enables the cartridge's own button check and direct transition to State 2
 *   (Title Screen).
 * - $04ED: high byte of the fade register $04EC (non-zero while fading).
 * - $00BC: bit 0 is set by $B2:8603 once fade-out has been triggered.
 * - $00F4: 16-bit scene frame counter (State 3 waits until $005A frames).
 */
enum {
  kDkc3IntroMainLoopVectorAddr = 0x004Eu,
  kDkc3IntroFadeOutFlagAddr = 0x00BCu,
  kDkc3IntroFrameTimerAddr = 0x00F4u,
  kDkc3IntroFadeStepAddr = 0x04EDu,
  kDkc3IntroFlagsAddr = 0x06ABu,

  kDkc3IntroMainLoopRareLogo = 0xB28751u,
  kDkc3IntroMainLoopCredits = 0xB28B2Cu,
  kDkc3IntroMainLoopCopyright = 0xB28EE5u,

  kDkc3IntroFlagsSkippableToTitle = 0x0005u,
  kDkc3IntroCopyrightFadeFrames = 0x005Au,

  /* Packed SNES controller bit for Start in Snes::input1_currentState. */
  kDkc3SnesInputStartBit = (1u << 3),
};

/* Returns true unless env_value is explicitly "0". */
bool Dkc3IntroSkipEnabledFromEnv(const char *env_value);

/* Allows the Start button to skip DKC3's cold-boot opening scenes (Rare logo,
 * bouncing/developer credit scene, and copyright notice) cleanly through the
 * cartridge's own fade-out and transition to the Title Screen.
 *
 * Returns true if WRAM was updated on this frame, false otherwise. Does
 * nothing when disabled, when Start is not pressed, while a fade is active,
 * or outside the three opening scenes. */
bool Dkc3ApplyIntroStartSkip(uint8_t *wram, size_t wram_size,
                             uint16_t input1_state, bool enabled);

#ifdef __cplusplus
}
#endif

#endif
