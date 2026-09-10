#ifndef DKC3_MSU1_H
#define DKC3_MSU1_H

#include <stddef.h>
#include <stdint.h>

typedef struct Dkc3Msu1 Dkc3Msu1;

/* Opens an extracted MSU-1 music directory containing track-N.pcm,
 * dkc3_msu-N.pcm, or dkc3_msu1-N.pcm files. The returned object is host-only
 * and never enters a cartridge save state. */
Dkc3Msu1 *Dkc3Msu1Open(const char *directory, char *error,
                        size_t error_size);
void Dkc3Msu1Close(Dkc3Msu1 *player);

/* Applies Conn's two-byte SPC music mute to an already checksum-verified
 * mutable DKC3 USA (En,Fr) ROM image. Stock sound effects remain active. */
int Dkc3Msu1ApplySpcMusicMute(uint8_t *rom, size_t rom_size,
                              char *error, size_t error_size);

/* DKC3 keeps its current song number at direct-page $08. Normal song numbers
 * are also the MSU-1 pack's track numbers. */
void Dkc3Msu1ObserveSong(Dkc3Msu1 *player, uint16_t song);

/* The original MSU-1 patch maps transition commands 1-10 to tracks 49-58 and
 * maps command 11 to track 53. This starts the matching one-shot/loop without
 * changing cartridge state. */
void Dkc3Msu1ObserveTransition(Dkc3Msu1 *player, uint16_t command);

void Dkc3Msu1Reset(Dkc3Msu1 *player);

/* Mixes 44.1-kHz signed stereo MSU PCM into the stock SPC sound effects. */
void Dkc3Msu1Mix(Dkc3Msu1 *player, int16_t *samples, int frames,
                 int channels, int output_rate);

unsigned Dkc3Msu1CurrentTrack(const Dkc3Msu1 *player);
const char *Dkc3Msu1Directory(const Dkc3Msu1 *player);

#endif
