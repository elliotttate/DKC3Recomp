#include "dkc3_msu1.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                       \
      fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__,    \
              #condition);                                                    \
      return 1;                                                               \
    }                                                                         \
  } while (0)

static void WriteLittle16(uint8_t bytes[2], int16_t value) {
  const uint16_t bits = (uint16_t)value;
  bytes[0] = (uint8_t)bits;
  bytes[1] = (uint8_t)(bits >> 8);
}

static int WriteTrack(const char *directory, const char *name,
                      uint32_t loop_frame, int16_t base) {
  char path[4096];
  if (snprintf(path, sizeof path, "%s/%s", directory, name) >=
      (int)sizeof path)
    return 0;
  FILE *stream = fopen(path, "wb");
  if (!stream)
    return 0;
  const uint8_t header[8] = {
      'M', 'S', 'U', '1',
      (uint8_t)loop_frame, (uint8_t)(loop_frame >> 8),
      (uint8_t)(loop_frame >> 16), (uint8_t)(loop_frame >> 24),
  };
  int ok = fwrite(header, 1, sizeof header, stream) == sizeof header;
  for (int frame = 0; ok && frame < 4; frame++) {
    uint8_t sample[4];
    WriteLittle16(sample, (int16_t)(base + frame * 100));
    WriteLittle16(sample + 2, (int16_t)(-base - frame * 100));
    ok = fwrite(sample, 1, sizeof sample, stream) == sizeof sample;
  }
  ok = fclose(stream) == 0 && ok;
  return ok;
}

int main(int argc, char **argv) {
  CHECK(argc == 2);
  const char *directory = argv[1];
  CHECK(mkdir(directory, 0755) == 0 || errno == EEXIST);

  /* Exercise every supported community-pack filename prefix. */
  CHECK(WriteTrack(directory, "dkc3_msu-1.pcm", 1, 1000));
  CHECK(WriteTrack(directory, "track-20.pcm", 1, 2000));
  CHECK(WriteTrack(directory, "dkc3_msu1-49.pcm", 1, 3000));
  CHECK(WriteTrack(directory, "track-50.pcm", 1, 4000));
  CHECK(WriteTrack(directory, "dkc3_msu-53.pcm", 1, 5000));

  char error[256] = {0};
  Dkc3Msu1 *player = Dkc3Msu1Open(directory, error, sizeof error);
  CHECK(player != NULL);
  CHECK(strcmp(Dkc3Msu1Directory(player), directory) == 0);

  Dkc3Msu1ObserveSong(player, 1);
  CHECK(Dkc3Msu1CurrentTrack(player) == 1);
  int16_t mixed[8] = {10, -10, 20, -20, 30, -30, 40, -40};
  Dkc3Msu1Mix(player, mixed, 4, 2, 44100);
  CHECK(mixed[0] == 1010 && mixed[1] == -1010);
  CHECK(mixed[2] == 1120 && mixed[3] == -1120);
  CHECK(mixed[4] == 1230 && mixed[5] == -1230);
  CHECK(mixed[6] == 1340 && mixed[7] == -1340);

  /* A transition overrides the unchanged song until the game selects a
   * different song. Command 11 intentionally aliases transition 5. */
  Dkc3Msu1ObserveTransition(player, 1);
  CHECK(Dkc3Msu1CurrentTrack(player) == 49);
  Dkc3Msu1ObserveSong(player, 1);
  CHECK(Dkc3Msu1CurrentTrack(player) == 49);
  Dkc3Msu1ObserveTransition(player, 2);
  CHECK(Dkc3Msu1CurrentTrack(player) == 50);
  Dkc3Msu1ObserveTransition(player, 11);
  CHECK(Dkc3Msu1CurrentTrack(player) == 53);
  Dkc3Msu1ObserveTransition(player, 12);
  CHECK(Dkc3Msu1CurrentTrack(player) == 53);
  Dkc3Msu1ObserveSong(player, 20);
  CHECK(Dkc3Msu1CurrentTrack(player) == 20);
  Dkc3Msu1ObserveSong(player, 0);
  CHECK(Dkc3Msu1CurrentTrack(player) == 0);

  Dkc3Msu1Reset(player);
  CHECK(Dkc3Msu1CurrentTrack(player) == 0);
  Dkc3Msu1Close(player);

  enum { kMuteOffset = 0x2D02D3 };
  const size_t rom_size = (size_t)kMuteOffset + 2u;
  uint8_t *rom = (uint8_t *)calloc(rom_size, 1);
  CHECK(rom != NULL);
  rom[kMuteOffset] = 0xD0;
  rom[kMuteOffset + 1] = 0x05;
  CHECK(Dkc3Msu1ApplySpcMusicMute(rom, rom_size, error, sizeof error));
  CHECK(rom[kMuteOffset] == 0 && rom[kMuteOffset + 1] == 0);
  CHECK(!Dkc3Msu1ApplySpcMusicMute(rom, rom_size, error, sizeof error));
  free(rom);

  puts("dkc3 msu1 tests passed");
  return 0;
}
