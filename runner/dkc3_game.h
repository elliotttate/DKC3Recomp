#ifndef DKC3_GAME_H
#define DKC3_GAME_H

#include <stddef.h>
#include <stdint.h>

#include "common_cpu_infra.h"

#ifdef __cplusplus
extern "C" {
#endif

const RtlGameInfo *Dkc3GameInfo(void);
void Dkc3BeginDrawing(uint8_t *pixels, size_t pitch);
void Dkc3DrawPpuFrame(void);
uint32_t Dkc3ResumePc(void);
int Dkc3LastLleResult(void);

#ifdef __cplusplus
}
#endif

#endif
