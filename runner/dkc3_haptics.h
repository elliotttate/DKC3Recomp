#ifndef DKC3_HAPTICS_H
#define DKC3_HAPTICS_H

#include <stdbool.h>
#include <stdint.h>

enum {
  DKC3_HAPTICS_ENEMY_SLOT_COUNT = 26,
};

typedef struct Dkc3EnemyDefeatProbe {
  uint16_t player_slot;
  uint16_t actor_type;
  uint16_t enemy_types[DKC3_HAPTICS_ENEMY_SLOT_COUNT];
  uint32_t defeated_enemy_mask;
  int16_t vertical_velocity;
  bool valid;
} Dkc3EnemyDefeatProbe;

/* Capture the active player and all non-Kong sprite slots immediately before
 * one cartridge frame. */
void Dkc3EnemyDefeatProbeCapture(Dkc3EnemyDefeatProbe *probe,
                                 const uint8_t *wram);

/* Returns true only when a descending player rebounds upward during the same
 * frame that a previously live enemy enters DKC3's standard defeated-sprite
 * state. A jump without a defeated enemy, or an enemy defeated by another
 * cause without a player rebound, fails closed. */
bool Dkc3EnemyDefeatProbeAccepted(const Dkc3EnemyDefeatProbe *probe,
                                  const uint8_t *wram);

#endif
