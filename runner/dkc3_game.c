#include "dkc3_game.h"
#include "dkc3_video.h"

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "snes/snes.h"

#include <stdbool.h>
#include <string.h>

enum {
  /* DKC3 USA vectors from the cartridge header: reset $00:80C4 and native
   * NMI $00:CA45. The runtime's pc24 convention folds the system banks to
   * bank 00; the code lives in the $80 mirror of ROM bank $C0. The NMI
   * handler saves the registers and jumps through the frame pointer at
   * direct-page $4A rather than returning, the same non-returning frame
   * dispatcher Rare used in DKC2. */
  kDkc3ResetPc = 0x0080C4,
  kDkc3NmiPc = 0x00CA45,
};

static bool s_cpu_initialized;
static uint32_t s_resume_pc = kDkc3ResetPc;
static int s_last_lle_result = 1;
static uint64_t s_next_frame_master;

typedef struct Dkc3HostSnapshot {
  CpuState cpu;
  uint32_t resume_pc;
  uint64_t next_frame_master;
  uint64_t main_cpu_cycles_estimate;
  uint64_t apu_pace_cycles_estimate;
  uint64_t apu_last_sync_cycles;
  uint64_t apu_last_sync_master;
  int last_lle_result;
  int frame_counter;
  uint8_t cpu_initialized;
  uint8_t last_hdmaen;
  uint8_t memsel;
} Dkc3HostSnapshot;

enum {
  /* NTSC master clocks per non-short host frame. A deadline at this cadence
   * lets VBlank interrupt long loading code instead of running many console
   * frames to the next WAI. */
  kDkc3NtscFrameMasterClocks = 1364 * 262,
};

static void Dkc3RunOneFrame(void) {
  bool first_frame = !s_cpu_initialized;
  if (s_next_frame_master == 0) {
    s_next_frame_master =
        g_cpu.master_cycles + kDkc3NtscFrameMasterClocks;
  }
  while (s_next_frame_master <= g_cpu.master_cycles)
    s_next_frame_master += kDkc3NtscFrameMasterClocks;
  interp_bridge_set_master_deadline(s_next_frame_master);

  if (first_frame) {
    cpu_state_init(&g_cpu, g_ram);
    s_cpu_initialized = true;
  }
  if (!first_frame && g_snes->nmiEnabled) {
    /* The main loop parks at WAI; the NMI performs the frame's VBlank work
     * and continues through its frame pointer. Push the interrupt frame at
     * the parked PC and run the handler and its continuation to the next
     * quiescent wait, which serves both an RTI handler and the
     * non-returning dispatcher. */
    g_snes->inNmi = true;
    cpu_push_interrupt_frame_at(&g_cpu, s_resume_pc);
    s_last_lle_result =
        interp_bridge_run_until_quiescent(&g_cpu, kDkc3NmiPc);
  } else {
    s_last_lle_result =
        interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
  }

  interp_bridge_set_master_deadline(0);
  s_resume_pc = interp_bridge_lle_resume_pc();
  if (g_cpu.master_cycles < s_next_frame_master) {
    g_cpu.master_cycles = s_next_frame_master;
    snes_sync_master_clock(g_snes, g_cpu.master_cycles);
  }
  s_next_frame_master += kDkc3NtscFrameMasterClocks;
}

static void Dkc3SaveExtra(SaveLoadInfo *sli) {
  Dkc3HostSnapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  snapshot.cpu = g_cpu;
  snapshot.cpu.ram = NULL;
  snapshot.resume_pc = s_resume_pc;
  snapshot.next_frame_master = s_next_frame_master;
  snapshot.main_cpu_cycles_estimate = g_main_cpu_cycles_estimate;
  snapshot.apu_pace_cycles_estimate = g_apu_pace_cycles_estimate;
  snapshot.apu_last_sync_cycles = g_apu_last_sync_cycles;
  snapshot.apu_last_sync_master = g_apu_last_sync_master;
  snapshot.last_lle_result = s_last_lle_result;
  snapshot.frame_counter = snes_frame_counter;
  snapshot.cpu_initialized = s_cpu_initialized ? 1u : 0u;
  snapshot.last_hdmaen = g_snesrecomp_last_hdmaen;
  snapshot.memsel = g_memsel;
  sli->func(sli, &snapshot, sizeof snapshot);
}

static void Dkc3LoadExtra(SaveLoadInfo *sli, uint32_t version) {
  (void)version;
  Dkc3HostSnapshot snapshot;
  sli->func(sli, &snapshot, sizeof snapshot);
  g_cpu = snapshot.cpu;
  g_cpu.ram = g_ram;
  s_resume_pc = snapshot.resume_pc;
  s_next_frame_master = snapshot.next_frame_master;
  g_main_cpu_cycles_estimate = snapshot.main_cpu_cycles_estimate;
  g_apu_pace_cycles_estimate = snapshot.apu_pace_cycles_estimate;
  g_apu_last_sync_cycles = snapshot.apu_last_sync_cycles;
  g_apu_last_sync_master = snapshot.apu_last_sync_master;
  s_last_lle_result = snapshot.last_lle_result;
  snes_frame_counter = snapshot.frame_counter;
  s_cpu_initialized = snapshot.cpu_initialized != 0;
  g_snesrecomp_last_hdmaen = snapshot.last_hdmaen;
  g_memsel = snapshot.memsel;
}

static void Dkc3OnStateLoaded(uint32_t version) {
  (void)version;
  g_cpu.ram = g_ram;
  g_apu_last_sync_master = g_cpu.master_cycles;
  g_snes->beamMasterLast = g_cpu.master_cycles;
  interp_bridge_set_master_deadline(0);
  Dkc3VideoSetTerrainReady(false);
}

static const RtlGameInfo kDkc3GameInfo = {
  .title = "dkc3",
  .initialize = NULL,
  .run_frame = &Dkc3RunOneFrame,
  .draw_ppu_frame = &Dkc3DrawPpuFrame,
  .save_name_prefix = "dkc3s",
  .state_save_extra = &Dkc3SaveExtra,
  .state_load_extra = &Dkc3LoadExtra,
  .on_state_loaded = &Dkc3OnStateLoaded,
};

const RtlGameInfo *Dkc3GameInfo(void) {
  return &kDkc3GameInfo;
}

void Dkc3BeginDrawing(uint8_t *pixels, size_t pitch) {
  PpuBeginDrawing(g_ppu, pixels, pitch, kPpuRenderFlags_NewRenderer);
}

void Dkc3DrawPpuFrame(void) {
  SimpleHdma channels[8];
  bool active[8] = {false};

  /* The native frame is presented as the cartridge renders it. A wider
   * aspect centers that frame between black margins: no layer is widened
   * and the margins are cleared after the render, so nothing the cartridge
   * did not draw ever appears beside the game. Widescreen terrain
   * reconstruction is later work with its own evidence gates. */
  const int extra = Dkc3VideoExtra();
  PpuSetWidescreenLayerMask(g_ppu, 0);
  PpuSetExtraSpace(g_ppu, (uint8_t)extra);

  dma_startDma(g_dma, g_snesrecomp_last_hdmaen, true);
  for (int channel = 0; channel < 8; channel++) {
    active[channel] = g_dma->channel[channel].hdmaActive;
    if (active[channel])
      SimpleHdma_Init(&channels[channel], &g_dma->channel[channel]);
  }

  for (int line = 0; line <= 224; line++) {
    ppu_runLine(g_ppu, line);
    for (int channel = 0; channel < 8; channel++) {
      if (active[channel]) SimpleHdma_DoLine(&channels[channel]);
    }
  }

  if (extra > 0) {
    const size_t width = (size_t)Dkc3VideoWidth();
    const size_t side_bytes = (size_t)extra * kDkc3VideoBytesPerPixel;
    const size_t right_offset =
        (width - (size_t)extra) * kDkc3VideoBytesPerPixel;
    for (int y = 0; y < kDkc3VideoHeight; y++) {
      uint8_t *row = g_ppu->renderBuffer + (size_t)y * g_ppu->renderPitch;
      memset(row, 0, side_bytes);
      memset(row + right_offset, 0, side_bytes);
    }
  }

  /* Model the VBlank boundary after the visible lines so the PPU reloads
   * its internal OAM port from OAMADD before the next frame's OAM DMA. */
  (void)ppu_checkOverscan(g_ppu);
  ppu_handleVblank(g_ppu);
}

uint32_t Dkc3ResumePc(void) {
  return s_resume_pc;
}

int Dkc3LastLleResult(void) {
  return s_last_lle_result;
}

/* Required neutral hooks declared by generated funcs.h. */
void RunOneFrameOfGame_Internal(void) {
  Dkc3RunOneFrame();
}

void ResetSpritesFunc(int first) {
  (void)first;
}
