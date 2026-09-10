#include "dkc3_spc_music.h"

#include "snes/apu.h"
#include "snes/dsp.h"

#include <string.h>

enum {
  /* $B2:82E5 uploads ROM $ED:0088 at SPC $0560. The music scheduler
   * branch at SPC $07AB is the two-byte mute at ROM offset $2D02D3.
   * Compare the entire surrounding scheduler against the verified image,
   * excluding only those two policy bytes; no engine code is embedded here. */
  kRomScheduler = 0x2D02D1,
  kSpcScheduler = 0x07A9,
  kSchedulerSize = 0x32,
  kBranchOffset = 2,
  /* The eight physical voices are borrowed by SFX when these flags are set
   * ($07B7 in the SPC scheduler); zero means a music-owned voice. */
  kSfxOwnership = 0x01E0,
};

static int KnownPolicy(const uint8_t *bytes) {
  return (bytes[0] == 0xD0 && bytes[1] == 0x05) ||
         (bytes[0] == 0x00 && bytes[1] == 0x00);
}

int Dkc3RestoreSpcMusicPolicy(struct Apu *apu, const uint8_t *verified_rom,
                              size_t rom_size) {
  if (!apu || !apu->dsp || !verified_rom ||
      rom_size < kRomScheduler + kSchedulerSize)
    return 0;
  uint8_t *loaded = apu->ram + kSpcScheduler;
  const uint8_t *wanted = verified_rom + kRomScheduler;
  if (!KnownPolicy(loaded + kBranchOffset) ||
      !KnownPolicy(wanted + kBranchOffset) ||
      memcmp(loaded, wanted, kBranchOffset) != 0 ||
      memcmp(loaded + kBranchOffset + 2, wanted + kBranchOffset + 2,
             kSchedulerSize - kBranchOffset - 2) != 0)
    return 0;
  if (memcmp(loaded + kBranchOffset, wanted + kBranchOffset, 2) == 0)
    return 1;

  Dsp *dsp = apu->dsp;
  const int muting = wanted[kBranchOffset] == 0;
  /* Do not let malformed echo metadata turn a policy repair into a broad
   * APU-memory write. Real DSP delay/index ranges are at most 15 * 512. */
  const size_t current_echo_end =
      (size_t)dsp->echoBufferIndex + dsp->echoRemain;
  const size_t echo_frames = dsp->echoDelay > current_echo_end
      ? dsp->echoDelay : current_echo_end;
  if (muting && (echo_frames == 0 || echo_frames > 15u * 512u))
    return 0;
  memcpy(loaded + kBranchOffset, wanted + kBranchOffset, 2);
  if (!muting)
    return 1;

  /* Skipping future music notes does not stop notes already sounding in an
   * old snapshot. Release only music-owned voices, including pending key-ons.
   * SFX voices keep their full envelopes, sample positions and volume. */
  for (unsigned voice = 0; voice < 8; voice++) {
    if (apu->ram[kSfxOwnership + voice])
      continue;
    dsp->channel[voice].keyOn = false;
    dsp->channel[voice].adsrState = 4;
    dsp->channel[voice].gain = 0;
    dsp->channel[voice].sampleOut = 0;
    dsp->ram[(voice << 4) | 8] = 0;
    dsp->ram[(voice << 4) | 9] = 0;
  }
  /* Queued dry audio and the shared echo contain the old soundtrack too.
   * Discard them once at the policy transition, not on ordinary loads or
   * rewind frames already using the selected soundtrack. A shared SFX echo
   * tail is necessarily discarded as well; new effects still render normally. */
  dsp->sampleRead = dsp->sampleWrite;
  memset(dsp->sampleBuffer, 0, sizeof dsp->sampleBuffer);
  memset(dsp->firBufferL, 0, sizeof dsp->firBufferL);
  memset(dsp->firBufferR, 0, sizeof dsp->firBufferR);
  for (size_t i = 0; i < echo_frames * 4u; i++)
    apu->ram[(uint16_t)(dsp->echoBufferAdr + i)] = 0;
  return 1;
}
