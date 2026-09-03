#ifndef DKC3_HDMA_H
#define DKC3_HDMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Host-only dry run of the HDMA tables the cartridge has already built for
 * the frame about to be rendered. The shared runner applies those tables to
 * the PPU one scanline at a time while rendering; this pass walks the same
 * tables in advance and records, for every rendered scanline, the BG scroll
 * and screen-enable values that will be in effect. Consecutive scanlines with
 * identical values form a band. The widescreen adapter decides a presentation
 * policy per (layer, band) from that exact geometry instead of inferring band
 * edges from register deltas while drawing.
 *
 * Nothing here writes guest memory, PPU registers, or DMA channel state.
 */

enum {
  kDkc3HdmaFirstLine = 1,
  kDkc3HdmaLastLine = 224,
  kDkc3HdmaMaxBands = 224,
};

typedef struct Dkc3HdmaBand {
  uint8_t first_line; /* inclusive rendered scanline, 1..224 */
  uint8_t last_line;  /* inclusive */
  uint16_t h_scroll[4];
  uint16_t v_scroll[4];
  uint8_t main_layers;
  uint8_t sub_layers;
  uint8_t bg_sc[4]; /* BG1SC..BG4SC: tilemap base and size per layer */
} Dkc3HdmaBand;

typedef struct Dkc3HdmaBands {
  int count;
  Dkc3HdmaBand band[kDkc3HdmaMaxBands];
  uint8_t band_for_line[kDkc3HdmaLastLine + 1];
} Dkc3HdmaBands;

typedef struct Dkc3HdmaChannelConfig {
  bool active;
  bool indirect;
  uint8_t b_address;      /* B-bus register offset $00-$3F */
  uint8_t mode;           /* transfer mode 0-7 */
  uint8_t indirect_bank;
  uint32_t table_address; /* 24-bit A-bus address of the table */
} Dkc3HdmaChannelConfig;

typedef struct Dkc3HdmaFrameState {
  uint16_t h_scroll[4];
  uint16_t v_scroll[4];
  uint8_t main_layers;
  uint8_t sub_layers;
  uint8_t bg_sc[4];
  /* The PPU's shared BG offset write latch. */
  uint8_t scroll_prev;
  uint8_t scroll_prev2;
} Dkc3HdmaFrameState;

/* Guest-address resolution supplied by the host. `pointer` maps a 24-bit
 * address to host memory exactly like the runner's HDMA path; `readable`
 * reports whether `length` bytes starting at that host pointer stay inside
 * a backing store. Both may be exercised with synthetic memory in tests. */
typedef struct Dkc3HdmaMemory {
  const uint8_t *(*pointer)(void *context, uint32_t address);
  bool (*readable)(void *context, const uint8_t *pointer, size_t length);
  void *context;
} Dkc3HdmaMemory;

void Dkc3HdmaScanBands(const Dkc3HdmaChannelConfig channels[8],
                       const Dkc3HdmaFrameState *start,
                       const Dkc3HdmaMemory *memory,
                       Dkc3HdmaBands *out);

/* The band that covers a rendered scanline. Line 0 is not rendered and maps
 * to the first band. Returns NULL when no bands were scanned. */
const Dkc3HdmaBand *Dkc3HdmaBandForLine(const Dkc3HdmaBands *bands,
                                        int line);

#endif
