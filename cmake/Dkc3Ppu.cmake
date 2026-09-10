# Keep runtime adaptations reproducible without editing the pinned submodule.
set(DKC3_PPU_SOURCE "${SNESRECOMP_ROOT}/runner/src/snes/ppu.c")
set(DKC3_PATCHED_PPU "${CMAKE_CURRENT_BINARY_DIR}/dkc3-runtime/ppu.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DKC3_PPU_SOURCE}")
file(READ "${DKC3_PPU_SOURCE}" dkc3_ppu)
set(dkc3_old_priority "    PpuDrawBackground_4bpp_opt(ppu, y, sub, 0, 0xc000, 0x8000);\n    PpuDrawBackground_4bpp_opt(ppu, y, sub, 1, 0xb100, 0x7100);")
string(FIND "${dkc3_ppu}" "${dkc3_old_priority}" dkc3_priority_anchor)
if(dkc3_priority_anchor EQUAL -1)
    message(FATAL_ERROR "DKC3 Mode 2 priority anchor moved; review the runtime adaptation")
endif()
string(REPLACE "${dkc3_old_priority}" "" dkc3_without_priority "${dkc3_ppu}")
string(LENGTH "${dkc3_ppu}" dkc3_before_length)
string(LENGTH "${dkc3_without_priority}" dkc3_after_length)
string(LENGTH "${dkc3_old_priority}" dkc3_anchor_length)
math(EXPR dkc3_removed_length "${dkc3_before_length} - ${dkc3_after_length}")
if(NOT dkc3_removed_length EQUAL dkc3_anchor_length)
    message(FATAL_ERROR "DKC3 Mode 2 priority anchor is not unique")
endif()
# Mode 2, front to back: OBJ3 BG1H OBJ2 BG2H OBJ1 BG1L OBJ0 BG2L.
# OBJ retains the runtime's priorities 14/10/6/2; BG priorities fit between.
string(REPLACE "${dkc3_old_priority}"
    "    PpuDrawBackground_4bpp_opt(ppu, y, sub, 0, 0xc000, 0x4000);\n    PpuDrawBackground_4bpp_opt(ppu, y, sub, 1, 0x8100, 0x1100);"
    dkc3_ppu "${dkc3_ppu}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dkc3-runtime")
file(WRITE "${DKC3_PATCHED_PPU}.tmp" "${dkc3_ppu}")
configure_file("${DKC3_PATCHED_PPU}.tmp" "${DKC3_PATCHED_PPU}" COPYONLY)
file(REMOVE "${DKC3_PATCHED_PPU}.tmp")
