#include "dkc3_spc_music.h"
#include "snes/apu.h"
#include "snes/dsp.h"

#include <stdio.h>

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
  return 1; } } while (0)

/* Synthetic scheduler bytes only: no ROM, game engine or music fixture. */
enum { kRom = 0x2D02D1, kSpc = 0x7A9, kLength = 0x32 };
static uint8_t rom[kRom + kLength];
static Apu apu;
static Dsp dsp;

static void Setup(void) {
  memset(&apu, 0, sizeof apu);
  memset(&dsp, 0, sizeof dsp);
  apu.dsp = &dsp;
  dsp.apu_ram = apu.ram;
  for (unsigned i = 0; i < kLength; i++)
    rom[kRom + i] = apu.ram[kSpc + i] = (uint8_t)(0x40 + i);
  rom[kRom + 2] = rom[kRom + 3] = 0;
  apu.ram[kSpc + 2] = 0xD0;
  apu.ram[kSpc + 3] = 0x05;
  dsp.echoBufferAdr = 0xF800;
  dsp.echoDelay = 512;
  dsp.echoRemain = 512;
  dsp.sampleWrite = 100;
  dsp.sampleRead = 10;
  memset(apu.ram + 0xF800, 0x45, 2048);
  memset(dsp.sampleBuffer, 0x34, sizeof dsp.sampleBuffer);
  memset(dsp.firBufferL, 0x34, sizeof dsp.firBufferL);
  memset(dsp.firBufferR, 0x34, sizeof dsp.firBufferR);
  for (unsigned i = 0; i < 8; i++) {
    dsp.channel[i].gain = 1000;
    dsp.channel[i].sampleOut = 300;
    dsp.channel[i].keyOn = true;
    dsp.channel[i].adsrState = 2;
    dsp.channel[i].volumeL = 60;
    dsp.channel[i].volumeR = 70;
  }
}

int main(void) {
  for (unsigned sfx_mask = 0; sfx_mask <= 255; sfx_mask++) {
    Setup();
    DspChannel before[8];
    memcpy(before, dsp.channel, sizeof before);
    for (unsigned i = 0; i < 8; i++)
      apu.ram[0x1E0 + i] = (sfx_mask >> i) & 1;
    CHECK(Dkc3RestoreSpcMusicPolicy(&apu, rom, sizeof rom));
    CHECK(apu.ram[kSpc + 2] == 0 && apu.ram[kSpc + 3] == 0);
    for (unsigned i = 0; i < 8; i++) {
      if ((sfx_mask >> i) & 1) {
        CHECK(memcmp(&before[i], &dsp.channel[i], sizeof before[i]) == 0);
      } else {
        CHECK(dsp.channel[i].gain == 0 && dsp.channel[i].sampleOut == 0);
        CHECK(!dsp.channel[i].keyOn && dsp.channel[i].adsrState == 4);
        CHECK(dsp.channel[i].volumeL == 60 && dsp.channel[i].volumeR == 70);
      }
    }
    CHECK(dsp.sampleRead == dsp.sampleWrite);
    for (unsigned i = 0; i < 2048; i++) CHECK(apu.ram[0xF800 + i] == 0);
    for (unsigned i = 0; i < 8; i++)
      CHECK(dsp.firBufferL[i] == 0 && dsp.firBufferR[i] == 0);
    /* Same-policy rewind/load must not keep interrupting active audio. */
    dsp.channel[0].gain = 500;
    dsp.sampleWrite++;
    CHECK(Dkc3RestoreSpcMusicPolicy(&apu, rom, sizeof rom));
    CHECK(dsp.channel[0].gain == 500 && dsp.sampleRead + 1 == dsp.sampleWrite);
    /* Disabling MSU restores native sequencing even from an MSU-era save. */
    rom[kRom + 2] = 0xD0; rom[kRom + 3] = 0x05;
    CHECK(Dkc3RestoreSpcMusicPolicy(&apu, rom, sizeof rom));
    CHECK(apu.ram[kSpc + 2] == 0xD0 && apu.ram[kSpc + 3] == 0x05);
    CHECK(dsp.channel[0].gain == 500 && dsp.sampleRead + 1 == dsp.sampleWrite);
  }
  Setup();
  CHECK(!Dkc3RestoreSpcMusicPolicy(NULL, rom, sizeof rom));
  CHECK(!Dkc3RestoreSpcMusicPolicy(&apu, NULL, sizeof rom));
  CHECK(!Dkc3RestoreSpcMusicPolicy(&apu, rom, sizeof rom - 1));
  apu.ram[kSpc + 7] ^= 1;
  CHECK(!Dkc3RestoreSpcMusicPolicy(&apu, rom, sizeof rom));
  CHECK(apu.ram[kSpc + 2] == 0xD0 && dsp.channel[0].gain == 1000);
  Setup();
  apu.ram[kSpc + 2] = 0xFF;
  CHECK(!Dkc3RestoreSpcMusicPolicy(&apu, rom, sizeof rom));
  Setup();
  rom[kRom + 3] = 5;
  CHECK(!Dkc3RestoreSpcMusicPolicy(&apu, rom, sizeof rom));
  Setup();
  dsp.echoDelay = 65535;
  CHECK(!Dkc3RestoreSpcMusicPolicy(&apu, rom, sizeof rom));
  CHECK(apu.ram[kSpc + 2] == 0xD0 && dsp.channel[0].gain == 1000);
  puts("dkc3 SPC music policy tests passed");
  return 0;
}
