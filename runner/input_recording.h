#ifndef DKC3_INPUT_RECORDING_H
#define DKC3_INPUT_RECORDING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Dkc3InputRecorder {
  FILE *stream;
  unsigned long long frames;
} Dkc3InputRecorder;

bool Dkc3InputRecorderOpen(Dkc3InputRecorder *recorder, const char *path,
                           char *error, size_t error_size);
bool Dkc3InputRecorderWrite(Dkc3InputRecorder *recorder, uint32_t controller,
                            char *error, size_t error_size);
bool Dkc3InputRecorderClose(Dkc3InputRecorder *recorder,
                            char *error, size_t error_size);
bool Dkc3InputRecorderIsOpen(const Dkc3InputRecorder *recorder);

#endif
