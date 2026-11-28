#include "dkc3_intro_skip.h"

#include <string.h>

static uint16_t ReadLe16(const uint8_t *wram, size_t address) {
  return (uint16_t)wram[address] | ((uint16_t)wram[address + 1u] << 8);
}

static uint32_t ReadLe24(const uint8_t *wram, size_t address) {
  return (uint32_t)ReadLe16(wram, address) |
         ((uint32_t)wram[address + 2u] << 16);
}

static void WriteLe16(uint8_t *wram, size_t address, uint16_t value) {
  wram[address] = (uint8_t)(value & 0xFFu);
  wram[address + 1u] = (uint8_t)(value >> 8);
}

bool Dkc3IntroSkipEnabledFromEnv(const char *env_value) {
  if (!env_value)
    return true;
  return strcmp(env_value, "0") != 0;
}

bool Dkc3ApplyIntroStartSkip(uint8_t *wram, size_t wram_size,
                             uint16_t input1_state, bool enabled) {
  if (!enabled || !wram || wram_size <= kDkc3IntroFlagsAddr + 1u)
    return false;
  if ((input1_state & kDkc3SnesInputStartBit) == 0u)
    return false;
  if (wram[kDkc3IntroFadeStepAddr] != 0u)
    return false;
  if ((wram[kDkc3IntroFadeOutFlagAddr] & 0x01u) != 0u)
    return false;

  const uint32_t main_loop = ReadLe24(wram, kDkc3IntroMainLoopVectorAddr);
  if (main_loop == kDkc3IntroMainLoopRareLogo ||
      main_loop == kDkc3IntroMainLoopCredits) {
    const uint16_t flags = ReadLe16(wram, kDkc3IntroFlagsAddr);
    if ((flags & kDkc3IntroFlagsSkippableToTitle) ==
        kDkc3IntroFlagsSkippableToTitle)
      return false;
    WriteLe16(wram, kDkc3IntroFlagsAddr,
              (uint16_t)(flags | kDkc3IntroFlagsSkippableToTitle));
    return true;
  }

  if (main_loop == kDkc3IntroMainLoopCopyright) {
    const uint16_t timer = ReadLe16(wram, kDkc3IntroFrameTimerAddr);
    if (timer >= kDkc3IntroCopyrightFadeFrames)
      return false;
    WriteLe16(wram, kDkc3IntroFrameTimerAddr, kDkc3IntroCopyrightFadeFrames);
    return true;
  }

  return false;
}
