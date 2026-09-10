#ifndef DKC3_SPC_MUSIC_H
#define DKC3_SPC_MUSIC_H

#include <stddef.h>
#include <stdint.h>

struct Apu;

/* Reconcile a restored SPC engine with the current verified ROM's music
 * policy. Returns zero without changes if the engine is not recognized
 * (including an early-boot state before upload). Caller serializes APU access.
 * This does not change the snapshot format or persist the host's pack choice. */
int Dkc3RestoreSpcMusicPolicy(struct Apu *apu, const uint8_t *verified_rom,
                              size_t rom_size);

#endif
