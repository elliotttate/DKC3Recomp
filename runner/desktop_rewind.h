#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Dkc3RewindHistory {
  uint8_t *storage;
  size_t snapshot_size;
  size_t capacity;
  size_t count;
  size_t write_index;
} Dkc3RewindHistory;

bool Dkc3RewindHistoryInit(Dkc3RewindHistory *history,
                           size_t snapshot_size, size_t capacity);
void Dkc3RewindHistoryDestroy(Dkc3RewindHistory *history);
bool Dkc3RewindHistoryPush(Dkc3RewindHistory *history,
                           const void *snapshot);
bool Dkc3RewindHistoryPop(Dkc3RewindHistory *history, void *snapshot);
