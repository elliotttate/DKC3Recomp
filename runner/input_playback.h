#ifndef DKC3_INPUT_PLAYBACK_H
#define DKC3_INPUT_PLAYBACK_H

#include <stddef.h>
#include <stdint.h>

typedef struct Dkc3InputPlayback {
  uint32_t *frames;
  size_t count;
} Dkc3InputPlayback;

int Dkc3InputPlaybackLoad(const char *path, Dkc3InputPlayback *playback,
                          char *error, size_t error_size);
void Dkc3InputPlaybackFree(Dkc3InputPlayback *playback);
uint32_t Dkc3InputPlaybackFrame(const Dkc3InputPlayback *playback,
                                size_t frame);

#endif
