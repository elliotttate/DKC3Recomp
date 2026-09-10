# Keep the pinned runtime intact. Apply only the measured diagnostic hot-path
# change to a build-directory copy; a changed or repeated anchor needs review.
set(DKC3_BRIDGE_SOURCE "${SNESRECOMP_ROOT}/runner/src/snes/interp_bridge.c")
set(DKC3_PATCHED_BRIDGE "${CMAKE_CURRENT_BINARY_DIR}/dkc3-runtime/interp_bridge.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DKC3_BRIDGE_SOURCE}")
file(READ "${DKC3_BRIDGE_SOURCE}" dkc3_bridge)

foreach(dkc3_port_predicate IN ITEMS
        "bridge_is_apu_port(adr)"
        "(uint16_t)adrl == 0x2140"
        "bridge_is_apu_port(adrl)")
    set(dkc3_old_check "getenv(\"SNESRECOMP_APU_PORT_DIAG\") && ${dkc3_port_predicate}")
    string(REPLACE "${dkc3_old_check}" "" dkc3_without_check "${dkc3_bridge}")
    string(LENGTH "${dkc3_bridge}" dkc3_before_length)
    string(LENGTH "${dkc3_without_check}" dkc3_after_length)
    string(LENGTH "${dkc3_old_check}" dkc3_anchor_length)
    math(EXPR dkc3_removed_length "${dkc3_before_length} - ${dkc3_after_length}")
    if(NOT dkc3_removed_length EQUAL dkc3_anchor_length)
        message(FATAL_ERROR "DKC3 APU diagnostic anchor moved or is not unique: ${dkc3_port_predicate}")
    endif()
    # Ordinary RAM/ROM/PPU accesses need no environment lookup. Keep the
    # original presence semantics (even an empty value enables diagnostics)
    # and allow an in-process diagnostic client to change the environment.
    string(REPLACE "${dkc3_old_check}"
        "${dkc3_port_predicate} && getenv(\"SNESRECOMP_APU_PORT_DIAG\")"
        dkc3_bridge "${dkc3_bridge}")
endforeach()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dkc3-runtime")
file(WRITE "${DKC3_PATCHED_BRIDGE}.tmp" "${dkc3_bridge}")
configure_file("${DKC3_PATCHED_BRIDGE}.tmp" "${DKC3_PATCHED_BRIDGE}" COPYONLY)
file(REMOVE "${DKC3_PATCHED_BRIDGE}.tmp")
