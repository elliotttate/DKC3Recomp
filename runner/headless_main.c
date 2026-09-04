#include "dkc3_game.h"
#include "dkc3_video.h"
#include "input_playback.h"
#include "verified_rom.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "sha256.h"
#include "snes/apu.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/ws_shadow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The headless validation host: run a verified DKC3 ROM for a number of
 * frames with no window or audio device and report hashes and statistics.
 *
 * Environment:
 *   DKC3_ASPECT=4:3|16:10|16:9|21:9 presentation width (default 4:3)
 *   DKC3_SRAM_INPUT=<file>          exact battery-save image to start from
 *   DKC3_SAVESTATE_INPUT=<file>     quick save to restore after boot
 *   SNESRECOMP_INPUT_PLAY=<file>    scripted input ("MASK * N" lines)
 *   DKC3_FRAME_PPM=<file>           final frame as a binary PPM
 *   DKC3_FRAME_PPM_PREFIX=<prefix>  per-frame PPMs, with
 *   DKC3_FRAME_PPM_START/END/STEP   the frame range and stride
 *   DKC3_AUDIO_PCM=<file>           rendered audio as raw 16-bit stereo
 *   DKC3_WRAM_OUTPUT / DKC3_VRAM_OUTPUT / DKC3_OAM_OUTPUT  memory dumps
 *   DKC3_STATE_TRACE=1              print game-state transitions
 *   DKC3_PREFILL_TRACE=1            print the widescreen prefill per frame
 *   DKC3_TRACE_PC=<hex pc24>        print CPU state at each hit of a PC
 *   DKC3_OAM_TRACE=1                print, per frame, every sprite whose X
 *                                   lies outside the native 256 columns
 *   DKC3_SPAWN_TRACE=1              print each sprite slot the frame it
 *                                   comes alive, with its camera-relative
 *                                   position
 */

static void PrintHash(FILE *stream, const uint8_t hash[32]) {
  for (int i = 0; i < 32; i++) fprintf(stream, "%02x", hash[i]);
}

static uint16_t ReadWram16(size_t address) {
  return (uint16_t)(g_ram[address] | ((uint16_t)g_ram[address + 1] << 8));
}

static int WriteFramePpm(const char *path, const uint8_t *pixels,
                         size_t width, size_t height, size_t pitch) {
  FILE *stream = fopen(path, "wb");
  if (!stream) return 0;
  int ok = fprintf(stream, "P6\n%zu %zu\n255\n", width, height) > 0;
  for (size_t y = 0; ok && y < height; y++) {
    const uint8_t *row = pixels + y * pitch;
    for (size_t x = 0; ok && x < width; x++) {
      const uint8_t rgb[3] = { row[x * 4 + 2], row[x * 4 + 1],
                               row[x * 4] };
      ok = fwrite(rgb, 1, sizeof rgb, stream) == sizeof rgb;
    }
  }
  if (fclose(stream) != 0) ok = 0;
  return ok;
}

static int WriteBytes(const char *path, const void *data, size_t size) {
  FILE *stream = fopen(path, "wb");
  if (!stream) return 0;
  int ok = fwrite(data, 1, size, stream) == size;
  if (fclose(stream) != 0) ok = 0;
  return ok;
}

static int ParseFrameNumber(const char *text, long fallback, long *value) {
  if (!text || !*text) {
    *value = fallback;
    return 1;
  }
  char *end = NULL;
  long parsed = strtol(text, &end, 10);
  if (!end || *end != '\0' || parsed < 0)
    return 0;
  *value = parsed;
  return 1;
}

static unsigned long long s_trace_pc_hits;

static void TracePc(CpuState *cpu, uint32_t pc24) {
  s_trace_pc_hits++;
  if (s_trace_pc_hits <= 16 ||
      (s_trace_pc_hits & (s_trace_pc_hits - 1)) == 0) {
    fprintf(stderr,
            "dkc3_trace_pc hit=%llu frame=%d pc=$%06x a=$%04x x=$%04x "
            "y=$%04x s=$%04x db=$%02x p=$%02x\n",
            s_trace_pc_hits, snes_frame_counter, (unsigned)pc24, cpu->A,
            cpu->X, cpu->Y, cpu->S, cpu->DB, cpu->P);
    fflush(stderr);
  }
}

/* DKC3 keeps its NMI frame pointer at direct-page $4A and a frame counter
 * near the start of direct page; both are cheap, stable state signals for
 * the transition trace until the game's own mode words are mapped. */
enum { kDkc3FramePointer = 0x004A, kDkc3FrameCounter = 0x005A };

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "usage: dkc3_snesrecomp_headless <rom.sfc> [frames]\n");
    return 2;
  }
  long frame_limit = argc == 3 ? strtol(argv[2], NULL, 10) : 600;
  if (frame_limit < 1 || frame_limit > 1000000) {
    fprintf(stderr, "frames must be between 1 and 1000000\n");
    return 2;
  }

  size_t rom_size = 0;
  char rom_error[160];
  uint8_t *rom =
      Dkc3ReadVerifiedRom(argv[1], &rom_size, rom_error, sizeof rom_error);
  if (!rom) {
    fprintf(stderr, "%s: %s\n", rom_error, argv[1]);
    return 2;
  }

  Dkc3VideoAspect aspect = kDkc3VideoAspectNative;
  {
    const char *aspect_text = getenv("DKC3_ASPECT");
    if (aspect_text && *aspect_text &&
        !Dkc3VideoAspectFromName(aspect_text, &aspect)) {
      fprintf(stderr, "DKC3_ASPECT must be 4:3, 16:10, 16:9, or 21:9\n");
      free(rom);
      return 2;
    }
  }
  Dkc3VideoSetAspect(aspect);
  RtlRegisterGame(Dkc3GameInfo());
  if (!SnesInit(rom, (int)rom_size)) {
    fprintf(stderr, "snesrecomp rejected the verified ROM\n");
    free(rom);
    return 4;
  }

  const char *sram_input = getenv("DKC3_SRAM_INPUT");
  if (sram_input && *sram_input) {
    FILE *f = fopen(sram_input, "rb");
    bool loaded = f && g_sram && g_sram_size > 0 &&
                  fread(g_sram, 1, (size_t)g_sram_size, f) ==
                      (size_t)g_sram_size &&
                  fgetc(f) == EOF;
    if (f) fclose(f);
    if (!loaded) {
      fprintf(stderr, "unable to load exact SRAM image: %s\n", sram_input);
      free(rom);
      return 13;
    }
  }

  const char *trace_pc_text = getenv("DKC3_TRACE_PC");
  if (trace_pc_text && *trace_pc_text) {
    char *end = NULL;
    unsigned long trace_pc = strtoul(trace_pc_text, &end, 16);
    if (!end || *end != '\0' || trace_pc > 0xfffffful) {
      fprintf(stderr, "DKC3_TRACE_PC must be a 24-bit hexadecimal address\n");
      free(rom);
      return 2;
    }
    interp_bridge_set_pre_opcode_hook((uint32_t)trace_pc, TracePc);
    fprintf(stderr, "dkc3_trace_pc armed=$%06lx\n", trace_pc);
  }

  enum {
    kBufferWidth = kDkc3VideoMaximumWidth,
    kHeight = kDkc3VideoHeight,
    kBytesPerPixel = kDkc3VideoBytesPerPixel
  };
  static uint8_t pixels[kBufferWidth * kHeight * kBytesPerPixel];
  const size_t frame_width = (size_t)Dkc3VideoWidth();
  const size_t frame_bytes = frame_width * kHeight * kBytesPerPixel;
  Dkc3BeginDrawing(pixels, frame_width * kBytesPerPixel);

  const char *frame_sequence_prefix = getenv("DKC3_FRAME_PPM_PREFIX");
  long frame_sequence_start = 0;
  long frame_sequence_end = frame_limit - 1;
  long frame_sequence_step = 1;
  if (frame_sequence_prefix && *frame_sequence_prefix &&
      (!ParseFrameNumber(getenv("DKC3_FRAME_PPM_START"), 0,
                         &frame_sequence_start) ||
       !ParseFrameNumber(getenv("DKC3_FRAME_PPM_END"), frame_limit - 1,
                         &frame_sequence_end) ||
       !ParseFrameNumber(getenv("DKC3_FRAME_PPM_STEP"), 1,
                         &frame_sequence_step) ||
       frame_sequence_step < 1 ||
       frame_sequence_start > frame_sequence_end ||
       frame_sequence_end >= frame_limit)) {
    fprintf(stderr,
            "invalid DKC3_FRAME_PPM_START/END/STEP sequence range\n");
    free(rom);
    return 18;
  }

  enum { kMaximumAudioFramesPerVideoFrame = 534 };
  int16_t audio[kMaximumAudioFramesPerVideoFrame * 2];
  const double audio_frames_per_video_frame = 32040.0 / 60.098811862;
  double audio_frame_accumulator = 0.0;
  unsigned long long audio_rendered_frames = 0;
  uint64_t audio_fnv1a = UINT64_C(14695981039346656037);
  FILE *audio_pcm = NULL;
  const char *audio_pcm_path = getenv("DKC3_AUDIO_PCM");
  if (audio_pcm_path && *audio_pcm_path) {
    audio_pcm = fopen(audio_pcm_path, "wb");
    if (!audio_pcm) {
      fprintf(stderr, "unable to open private audio output: %s\n",
              audio_pcm_path);
      free(rom);
      return 10;
    }
  }
  unsigned long video_active_frames = 0;
  unsigned long blank_frames = 0;
  unsigned long consecutive_blank_frames = 0;
  unsigned long max_consecutive_blank_frames = 0;
  unsigned long audio_active_frames = 0;
  unsigned long audio_silent_frames = 0;
  unsigned long long audio_nonzero_samples = 0;
  unsigned audio_peak = 0;

  int state_initialized = 0;
  uint16_t previous_frame_pointer = 0;
  uint8_t previous_bgmode = 0;
  uint8_t previous_inidisp = 0;
  unsigned state_events = 0;
  const char *state_trace_text = getenv("DKC3_STATE_TRACE");
  const int emit_state_trace =
      state_trace_text && *state_trace_text && *state_trace_text != '0';

  Dkc3InputPlayback input_playback = {0};
  {
    const char *p = getenv("SNESRECOMP_INPUT_PLAY");
    if (p && p[0]) {
      char error[192];
      if (!Dkc3InputPlaybackLoad(p, &input_playback, error, sizeof error)) {
        fprintf(stderr, "input_play: %s: %s\n", p, error);
        if (audio_pcm) fclose(audio_pcm);
        free(rom);
        return 17;
      }
      fprintf(stderr, "input_play: loaded %zu frames from %s\n",
              input_playback.count, p);
    }
  }

  const char *prefill_trace_text = getenv("DKC3_PREFILL_TRACE");
  const int prefill_trace =
      prefill_trace_text && *prefill_trace_text && *prefill_trace_text != '0';
  const char *savestate_input = getenv("DKC3_SAVESTATE_INPUT");
  bool savestate_pending = savestate_input && *savestate_input;

  const bool oam_trace = getenv("DKC3_OAM_TRACE") != NULL;
  const bool spawn_trace = getenv("DKC3_SPAWN_TRACE") != NULL;
  uint16_t spawn_state[24] = {0};
  for (long frame = 0; frame < frame_limit; frame++) {
    if (savestate_pending && frame == 2) {
      savestate_pending = false;
      if (!RtlLoadSnapshot(savestate_input)) {
        fprintf(stderr, "unable to restore the quick save: %s\n",
                savestate_input);
        if (audio_pcm) fclose(audio_pcm);
        Dkc3InputPlaybackFree(&input_playback);
        free(rom);
        return 14;
      }
      fprintf(stderr, "savestate: restored %s at host frame %ld\n",
              savestate_input, frame);
    }
    uint32_t _in = Dkc3InputPlaybackFrame(&input_playback, (size_t)frame);
    RtlRunFrame(_in);
    if (spawn_trace) {
      const uint16_t camera_x = ReadWram16(0x196d);
      const uint16_t camera_y = ReadWram16(0x1973);
      for (int slot = 0; slot < 24; slot++) {
        const unsigned base = 0x0878u + (unsigned)slot * 0x6eu;
        const uint16_t state = ReadWram16(base);
        if (state != 0 && spawn_state[slot] == 0) {
          fprintf(stderr,
                  "spawn frame=%ld slot=%d dx=%d dy=%d handler=%04x param=%04x\n",
                  frame, slot, (int)ReadWram16(base + 0x12) - (int)camera_x,
                  (int)ReadWram16(base + 0x16) - (int)camera_y,
                  ReadWram16(base + 2), ReadWram16(base + 0x0a));
        }
        spawn_state[slot] = state;
      }
    }
    if (getenv("DKC3_PPU_DUMP") &&
        (frame == frame_limit - 1 || getenv("DKC3_PPU_DUMP")[0] == '2')) {
      unsigned tint_seen = 0, tint_water = 0, tint_matched = 0;
      Dkc3TintLineCounters(&tint_seen, &tint_water, &tint_matched);
      fprintf(stderr, "ppu_frame %ld camera=[%04x,%04x] tint=%u/%u/%u ", frame,
              ReadWram16(0x196d), ReadWram16(0x1973), tint_seen, tint_water,
              tint_matched);
      fprintf(stderr,
              "ppu_state windowsel=%08x w1=[%u,%u] w2=[%u,%u] windowed=[%02x,%02x] "
              "enabled=[%02x,%02x] cgwsel=%02x cgadsub=%02x fixed=%04x\n",
              (unsigned)g_ppu->windowsel, g_ppu->window1left, g_ppu->window1right,
              g_ppu->window2left, g_ppu->window2right, g_ppu->screenWindowed[0],
              g_ppu->screenWindowed[1], g_ppu->screenEnabled[0],
              g_ppu->screenEnabled[1], g_ppu->cgwsel, g_ppu->cgadsub,
              g_ppu->fixedColor);
      for (int layer = 0; layer < 3; layer++) {
        WsShadowMarginStat stat;
        WsShadowGetMarginStats(layer, &stat);
        fprintf(stderr,
                "ws_margin layer=%d west=%llu/%llu east=%llu/%llu (hit/miss)\n",
                layer, (unsigned long long)stat.westHit,
                (unsigned long long)stat.westMiss,
                (unsigned long long)stat.eastHit,
                (unsigned long long)stat.eastMiss);
      }
      if (g_snes && g_snes->dma) {
        for (int channel = 0; channel < 8; channel++) {
          const DmaChannel *ch = &g_snes->dma->channel[channel];
          if (!ch->hdmaActive)
            continue;
          fprintf(stderr,
                  "hdma channel=%d target=$21%02x indirect=%d table=$%02x:%04x "
                  "mode=%u\n",
                  channel, ch->bAdr, ch->indirect ? 1 : 0, ch->aBank,
                  ch->aAdr, (unsigned)ch->mode);
        }
      }
    }
    if (oam_trace) {
      /* Decode OAM X the way the PPU does for a widened frame: the ninth
       * bit is positive up to 256 + extra, negative beyond. */
      const int extra = Dkc3VideoExtra();
      for (int index = 0; index < 128; index++) {
        const uint8_t *entry = g_ppu->oam + index * 4;
        int x = entry[0] |
                (((g_ppu->highOam[index >> 2] >> ((index & 3) * 2)) & 1)
                 << 8);
        if (x >= 256 + extra)
          x -= 512;
        const int y = entry[1];
        if (y >= 224 || (x >= 0 && x < 256))
          continue;
        fprintf(stderr, "oam frame=%ld index=%d x=%d y=%d tile=%02x attr=%02x\n",
                frame, index, x, y, entry[2], entry[3]);
      }
    }
    if (g_fail) {
      fprintf(stderr,
              "snesrecomp reported an off-rails runtime failure at host "
              "frame %ld resume=$%06x\n",
              frame, (unsigned)Dkc3ResumePc());
      if (audio_pcm) fclose(audio_pcm);
      Dkc3InputPlaybackFree(&input_playback);
      free(rom);
      return 6;
    }
    if (!Dkc3LastLleResult()) {
      fprintf(stderr,
              "LLE stopped at host frame %ld resume=$%06x x=$%04x "
              "apu_in=%02x%02x%02x%02x apu_out=%02x%02x%02x%02x "
              "spc_pc=$%04x ipl=%d\n",
              frame, (unsigned)Dkc3ResumePc(), g_cpu.X,
              g_snes->apu->inPorts[3], g_snes->apu->inPorts[2],
              g_snes->apu->inPorts[1], g_snes->apu->inPorts[0],
              g_snes->apu->outPorts[3], g_snes->apu->outPorts[2],
              g_snes->apu->outPorts[1], g_snes->apu->outPorts[0],
              g_snes->apu->spc->pc, g_snes->apu->romReadable ? 1 : 0);
      if (audio_pcm) fclose(audio_pcm);
      Dkc3InputPlaybackFree(&input_playback);
      free(rom);
      return 5;
    }
    Dkc3DrawPpuFrame();
    if (prefill_trace) {
      Dkc3TerrainPrefillStats prefill;
      Dkc3GetTerrainPrefillStats(&prefill);
      fprintf(stderr,
              "prefill frame=%ld counter=%u camera=[%04x,%04x] ready=%d bias=%d "
              "present=%u matching=%u margin_present=%u margin_matching=%u "
              "phase=[%u,%u] top=%u/%u\n",
              frame, (unsigned)Dkc3FrameCounter(), ReadWram16(0x196d),
              ReadWram16(0x1973),
              Dkc3VideoTerrainReady() ? 1 : 0, Dkc3VideoPresentationBias(),
              prefill.present,
              prefill.matching, prefill.margin_present,
              prefill.margin_matching, prefill.phase_h, prefill.phase_v,
              prefill.top_shadow_row, prefill.top_source_row);
    }
    if (frame_sequence_prefix && *frame_sequence_prefix &&
        frame >= frame_sequence_start && frame <= frame_sequence_end &&
        (frame - frame_sequence_start) % frame_sequence_step == 0) {
      char path[1024];
      int length = snprintf(path, sizeof path, "%s_%06ld.ppm",
                            frame_sequence_prefix, frame);
      if (length < 0 || (size_t)length >= sizeof path ||
          !WriteFramePpm(path, pixels, frame_width, kHeight,
                         frame_width * kBytesPerPixel)) {
        fprintf(stderr, "unable to write private frame sequence at %ld\n",
                frame);
        if (audio_pcm) fclose(audio_pcm);
        Dkc3InputPlaybackFree(&input_playback);
        free(rom);
        return 18;
      }
    }

    const uint16_t frame_pointer = ReadWram16(kDkc3FramePointer);
    int state_changed = !state_initialized ||
                        frame_pointer != previous_frame_pointer ||
                        g_ppu->bgmode != previous_bgmode ||
                        g_ppu->inidisp != previous_inidisp;
    if (state_changed) {
      state_events++;
      if (emit_state_trace) {
        fprintf(stderr,
                "state_event frame=%ld nmi_frame=$%04x bgmode=$%02x "
                "inidisp=$%02x main=$%02x frame_ctr=$%04x\n",
                frame + 1, frame_pointer, g_ppu->bgmode, g_ppu->inidisp,
                g_ppu->screenEnabled[0], ReadWram16(kDkc3FrameCounter));
      }
    }
    previous_frame_pointer = frame_pointer;
    previous_bgmode = g_ppu->bgmode;
    previous_inidisp = g_ppu->inidisp;
    state_initialized = 1;

    int frame_active = 0;
    for (size_t i = 0; i < frame_bytes; i++) {
      if (pixels[i] != 0) {
        frame_active = 1;
        break;
      }
    }
    if (frame_active) {
      video_active_frames++;
      consecutive_blank_frames = 0;
    } else {
      blank_frames++;
      consecutive_blank_frames++;
      if (consecutive_blank_frames > max_consecutive_blank_frames)
        max_consecutive_blank_frames = consecutive_blank_frames;
    }

    audio_frame_accumulator += audio_frames_per_video_frame;
    int audio_frames_this_frame = (int)audio_frame_accumulator;
    audio_frame_accumulator -= audio_frames_this_frame;
    if (audio_frames_this_frame < 0 ||
        audio_frames_this_frame > kMaximumAudioFramesPerVideoFrame) {
      fprintf(stderr, "invalid audio frame request: %d\n",
              audio_frames_this_frame);
      if (audio_pcm) fclose(audio_pcm);
      Dkc3InputPlaybackFree(&input_playback);
      free(rom);
      return 11;
    }
    size_t audio_samples_this_frame =
        (size_t)audio_frames_this_frame * 2u;
    memset(audio, 0, audio_samples_this_frame * sizeof audio[0]);
    RtlRenderAudio(audio, audio_frames_this_frame, 2);
    audio_rendered_frames += (unsigned)audio_frames_this_frame;
    int audio_active = 0;
    for (size_t i = 0; i < audio_samples_this_frame; i++) {
      int sample = audio[i];
      unsigned magnitude = (unsigned)(sample < 0 ? -sample : sample);
      if (magnitude != 0) {
        audio_active = 1;
        audio_nonzero_samples++;
        if (magnitude > audio_peak) audio_peak = magnitude;
      }
      audio_fnv1a ^= (uint8_t)(sample & 0xff);
      audio_fnv1a *= UINT64_C(1099511628211);
      audio_fnv1a ^= (uint8_t)(((uint16_t)sample >> 8) & 0xff);
      audio_fnv1a *= UINT64_C(1099511628211);
    }
    if (audio_pcm &&
        fwrite(audio, sizeof audio[0], audio_samples_this_frame, audio_pcm) !=
            audio_samples_this_frame) {
      fprintf(stderr, "unable to write private audio output: %s\n",
              audio_pcm_path);
      fclose(audio_pcm);
      Dkc3InputPlaybackFree(&input_playback);
      free(rom);
      return 12;
    }
    if (audio_active)
      audio_active_frames++;
    else
      audio_silent_frames++;
  }

  if (audio_pcm && fclose(audio_pcm) != 0) {
    fprintf(stderr, "unable to close private audio output: %s\n",
            audio_pcm_path);
    free(rom);
    Dkc3InputPlaybackFree(&input_playback);
    return 13;
  }
  audio_pcm = NULL;

  uint8_t frame_hash[32];
  uint8_t wram_hash[32];
  uint8_t vram_hash[32];
  uint8_t cgram_hash[32];
  uint8_t oam_hash[32];
  uint8_t oam_bytes[544];
  unsigned vram_words = 0;
  unsigned cgram_words = 0;
  for (size_t i = 0; i < sizeof g_ppu->vram / sizeof g_ppu->vram[0]; i++)
    if (g_ppu->vram[i] != 0) vram_words++;
  for (size_t i = 0; i < sizeof g_ppu->cgram / sizeof g_ppu->cgram[0]; i++)
    if (g_ppu->cgram[i] != 0) cgram_words++;
  sha256_compute(pixels, frame_bytes, frame_hash);
  sha256_compute(g_ram, 0x20000, wram_hash);
  sha256_compute((const uint8_t *)g_ppu->vram, sizeof g_ppu->vram, vram_hash);
  sha256_compute((const uint8_t *)g_ppu->cgram, sizeof g_ppu->cgram,
                 cgram_hash);
  memcpy(oam_bytes, g_ppu->oam, sizeof g_ppu->oam);
  memcpy(oam_bytes + sizeof g_ppu->oam, g_ppu->highOam,
         sizeof g_ppu->highOam);
  sha256_compute(oam_bytes, sizeof oam_bytes, oam_hash);
  printf("video_state inidisp=$%02x bgmode=$%02x main=$%02x sub=$%02x "
         "nmi=%d frame_counter=%d vram_words=%u cgram_words=%u "
         "nmi_frame=$%04x frame_ctr=$%04x aspect=%s width=%zu\n",
         g_ppu->inidisp, g_ppu->bgmode, g_ppu->screenEnabled[0],
         g_ppu->screenEnabled[1], g_snes->nmiEnabled ? 1 : 0,
         snes_frame_counter, vram_words, cgram_words,
         ReadWram16(kDkc3FramePointer), ReadWram16(kDkc3FrameCounter),
         Dkc3VideoAspectName(aspect), frame_width);
  printf("ppu_layers bgXsc=[%02x,%02x,%02x,%02x] bgTileAdr=$%04x "
         "hscroll=[%04x,%04x,%04x,%04x] vscroll=[%04x,%04x,%04x,%04x]\n",
         g_ppu->bgXsc[0], g_ppu->bgXsc[1], g_ppu->bgXsc[2], g_ppu->bgXsc[3],
         g_ppu->bgTileAdr, g_ppu->hScroll[0], g_ppu->hScroll[1],
         g_ppu->hScroll[2], g_ppu->hScroll[3], g_ppu->vScroll[0],
         g_ppu->vScroll[1], g_ppu->vScroll[2], g_ppu->vScroll[3]);
  printf("ppu_effects windowsel=$%08x w1=[%u,%u] w2=[%u,%u] "
         "wbgobjlog=$%04x main_window=$%02x sub_window=$%02x "
         "cgadsub=$%02x cgwsel=$%02x fixed=$%04x "
         "wide=$%02x repeat=$%02x clamp=$%02x bg3_y=%u "
         "window_expand=[$%02x,$%02x]\n",
         g_ppu->windowsel, g_ppu->window1left, g_ppu->window1right,
         g_ppu->window2left, g_ppu->window2right, g_ppu->wbgobjlog,
         g_ppu->screenWindowed[0], g_ppu->screenWindowed[1],
         g_ppu->cgadsub, g_ppu->cgwsel, g_ppu->fixedColor,
         g_ppu->wsLayerWidenMask, g_ppu->wsLayerRepeat,
         g_ppu->wsLayerClamp, g_ppu->wsBg3WidenY,
         g_ppu->wsWindowExpandLayers, g_ppu->wsWindowExpandWindows);
  {
    Dkc3TerrainPrefillStats prefill;
    Dkc3GetTerrainPrefillStats(&prefill);
    printf("widescreen terrain_ready=%d prefill present=%u matching=%u "
           "margin_present=%u margin_matching=%u\n",
           Dkc3VideoTerrainReady() ? 1 : 0, prefill.present,
           prefill.matching, prefill.margin_present, prefill.margin_matching);
  }
  printf("frame_sha256=");
  PrintHash(stdout, frame_hash);
  printf("\nwram_sha256=");
  PrintHash(stdout, wram_hash);
  printf("\nvram_sha256=");
  PrintHash(stdout, vram_hash);
  printf("\ncgram_sha256=");
  PrintHash(stdout, cgram_hash);
  printf("\noam_sha256=");
  PrintHash(stdout, oam_hash);
  printf("\nrun_stats video_active_frames=%lu blank_frames=%lu "
         "max_consecutive_blank_frames=%lu audio_active_frames=%lu "
         "audio_silent_frames=%lu audio_frames=%llu "
         "audio_nonzero_samples=%llu audio_peak=%u audio_fnv1a=%016llx "
         "state_events=%u",
         video_active_frames, blank_frames, max_consecutive_blank_frames,
         audio_active_frames, audio_silent_frames, audio_rendered_frames,
         audio_nonzero_samples, audio_peak,
         (unsigned long long)audio_fnv1a, state_events);
  const char *frame_output = getenv("DKC3_FRAME_PPM");
  if (frame_output && *frame_output) {
    if (!WriteFramePpm(frame_output, pixels, frame_width, kHeight,
                       frame_width * kBytesPerPixel)) {
      fprintf(stderr, "\nunable to write private frame output: %s\n",
              frame_output);
      Dkc3InputPlaybackFree(&input_playback);
      free(rom);
      return 7;
    }
    printf("\nframe_output=%s", frame_output);
  }
  const char *wram_output = getenv("DKC3_WRAM_OUTPUT");
  if (wram_output && *wram_output) {
    if (!WriteBytes(wram_output, g_ram, 0x20000)) {
      fprintf(stderr, "\nunable to write private WRAM output: %s\n",
              wram_output);
      Dkc3InputPlaybackFree(&input_playback);
      free(rom);
      return 9;
    }
    printf("\nwram_output=%s", wram_output);
  }
  const char *vram_output = getenv("DKC3_VRAM_OUTPUT");
  if (vram_output && *vram_output) {
    if (!WriteBytes(vram_output, g_ppu->vram, sizeof g_ppu->vram)) {
      fprintf(stderr, "\nunable to write private VRAM output: %s\n",
              vram_output);
      Dkc3InputPlaybackFree(&input_playback);
      free(rom);
      return 9;
    }
    printf("\nvram_output=%s", vram_output);
  }
  const char *oam_output = getenv("DKC3_OAM_OUTPUT");
  if (oam_output && *oam_output) {
    if (!WriteBytes(oam_output, oam_bytes, sizeof oam_bytes)) {
      fprintf(stderr, "\nunable to write private OAM output: %s\n",
              oam_output);
      Dkc3InputPlaybackFree(&input_playback);
      free(rom);
      return 9;
    }
    printf("\noam_output=%s", oam_output);
  }
  printf("\nresult=completed frames=%ld\n", frame_limit);
  free(rom);
  Dkc3InputPlaybackFree(&input_playback);
  return 0;
}
