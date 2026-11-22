#include "dkc3_intro_skip.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Fail(const char *message, int line) {
  fprintf(stderr, "test_dkc3_intro_skip.c:%d: %s\n", line, message);
  exit(1);
}

#define CHECK(cond) \
  do {              \
    if (!(cond))    \
      Fail(#cond, __LINE__); \
  } while (0)

static void SetMainLoop(uint8_t *wram, uint32_t pc24) {
  wram[kDkc3IntroMainLoopVectorAddr] = (uint8_t)(pc24 & 0xFFu);
  wram[kDkc3IntroMainLoopVectorAddr + 1u] = (uint8_t)((pc24 >> 8) & 0xFFu);
  wram[kDkc3IntroMainLoopVectorAddr + 2u] = (uint8_t)((pc24 >> 16) & 0xFFu);
}

static uint16_t Read16(const uint8_t *wram, size_t addr) {
  return (uint16_t)wram[addr] | ((uint16_t)wram[addr + 1u] << 8);
}

static void Write16(uint8_t *wram, size_t addr, uint16_t value) {
  wram[addr] = (uint8_t)(value & 0xFFu);
  wram[addr + 1u] = (uint8_t)(value >> 8);
}

int main(void) {
  CHECK(Dkc3IntroSkipEnabledFromEnv(NULL));
  CHECK(Dkc3IntroSkipEnabledFromEnv(""));
  CHECK(Dkc3IntroSkipEnabledFromEnv("1"));
  CHECK(!Dkc3IntroSkipEnabledFromEnv("0"));

  uint8_t wram[0x1000];
  memset(wram, 0, sizeof wram);

  /* Null / undersized buffers fail safely. */
  CHECK(!Dkc3ApplyIntroStartSkip(NULL, sizeof wram, kDkc3SnesInputStartBit, true));
  CHECK(!Dkc3ApplyIntroStartSkip(wram, kDkc3IntroFlagsAddr, kDkc3SnesInputStartBit, true));

  /* State 0 (Rare logo): does nothing without Start or when disabled. */
  SetMainLoop(wram, kDkc3IntroMainLoopRareLogo);
  CHECK(!Dkc3ApplyIntroStartSkip(wram, sizeof wram, 0u, true));
  CHECK(!Dkc3ApplyIntroStartSkip(wram, sizeof wram, (1u << 0) | (1u << 8), true));
  CHECK(!Dkc3ApplyIntroStartSkip(wram, sizeof wram, kDkc3SnesInputStartBit, false));
  CHECK(Read16(wram, kDkc3IntroFlagsAddr) == 0u);

  /* State 0: ignored during active fade-in/fade-out ($04ED != 0) or after fade-out flag ($00BC bit 0). */
  wram[kDkc3IntroFadeStepAddr] = 0x02u;
  CHECK(!Dkc3ApplyIntroStartSkip(wram, sizeof wram, kDkc3SnesInputStartBit, true));
  CHECK(Read16(wram, kDkc3IntroFlagsAddr) == 0u);
  wram[kDkc3IntroFadeStepAddr] = 0u;
  wram[kDkc3IntroFadeOutFlagAddr] = 0x01u;
  CHECK(!Dkc3ApplyIntroStartSkip(wram, sizeof wram, kDkc3SnesInputStartBit, true));
  CHECK(Read16(wram, kDkc3IntroFlagsAddr) == 0u);
  wram[kDkc3IntroFadeOutFlagAddr] = 0u;

  /* State 0 + Start: sets $06AB bits 0 and 2 ($0005) and is idempotent. */
  CHECK(Dkc3ApplyIntroStartSkip(wram, sizeof wram, kDkc3SnesInputStartBit, true));
  CHECK(Read16(wram, kDkc3IntroFlagsAddr) == kDkc3IntroFlagsSkippableToTitle);
  CHECK(!Dkc3ApplyIntroStartSkip(wram, sizeof wram, kDkc3SnesInputStartBit, true));

  /* State 1 (bouncing / developer credits): sets $06AB |= $0005 preserving existing bits. */
  memset(wram, 0, sizeof wram);
  SetMainLoop(wram, kDkc3IntroMainLoopCredits);
  Write16(wram, kDkc3IntroFlagsAddr, 0x0001u);
  CHECK(Dkc3ApplyIntroStartSkip(wram, sizeof wram, kDkc3SnesInputStartBit, true));
  CHECK(Read16(wram, kDkc3IntroFlagsAddr) == kDkc3IntroFlagsSkippableToTitle);

  /* State 3 (copyright screen): advances frame timer $00F4 to $005A. */
  memset(wram, 0, sizeof wram);
  SetMainLoop(wram, kDkc3IntroMainLoopCopyright);
  Write16(wram, kDkc3IntroFrameTimerAddr, 0x0010u);
  CHECK(Dkc3ApplyIntroStartSkip(wram, sizeof wram, kDkc3SnesInputStartBit, true));
  CHECK(Read16(wram, kDkc3IntroFrameTimerAddr) == kDkc3IntroCopyrightFadeFrames);
  CHECK(!Dkc3ApplyIntroStartSkip(wram, sizeof wram, kDkc3SnesInputStartBit, true));

  /* Title Screen ($B2:8E05) and level gameplay ($B3:8076) are untouched. */
  memset(wram, 0, sizeof wram);
  SetMainLoop(wram, 0xB28E05u);
  CHECK(!Dkc3ApplyIntroStartSkip(wram, sizeof wram, kDkc3SnesInputStartBit, true));
  CHECK(Read16(wram, kDkc3IntroFlagsAddr) == 0u);

  SetMainLoop(wram, 0xB38076u);
  CHECK(!Dkc3ApplyIntroStartSkip(wram, sizeof wram, kDkc3SnesInputStartBit, true));
  CHECK(Read16(wram, kDkc3IntroFlagsAddr) == 0u);

  return 0;
}
