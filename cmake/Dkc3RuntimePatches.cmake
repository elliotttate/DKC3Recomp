# DKC3 keeps the pinned snesrecomp submodule intact and applies its runtime
# adaptations to build-directory copies at configure time. The hunk files
# under cmake/runtime-patches name each source and the literal replacements;
# scripts/apply_dkc3_runtime_patches.py refuses a missing, moved, or
# duplicated anchor so the build fails closed when the pin changes.
#
# Sets:
#   DKC3_RUNTIME_PATCH_DIR       the hunk directory
#   DKC3_RUNTIME_PATCHED_DIR     where the copies live
#   DKC3_RUNTIME_PATCHED_SOURCES the copies (absolute paths)
#   DKC3_RUNTIME_ORIGINAL_SOURCES the pinned sources they replace
#   DKC3_PATCHED_PPU             the copy of runner/src/snes/ppu.c
# and provides dkc3_use_patched_runtime_sources(<list-var>) to swap the
# originals for the copies in a runtime source list.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(DKC3_RUNTIME_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime-patches")
set(DKC3_RUNTIME_PATCHED_DIR "${CMAKE_CURRENT_BINARY_DIR}/dkc3-runtime")
set(DKC3_RUNTIME_PATCH_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/apply_dkc3_runtime_patches.py")

file(GLOB DKC3_RUNTIME_HUNK_FILES CONFIGURE_DEPENDS
    "${DKC3_RUNTIME_PATCH_DIR}/*.hunks")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${DKC3_RUNTIME_HUNK_FILES} "${DKC3_RUNTIME_PATCH_SCRIPT}")

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${DKC3_RUNTIME_PATCH_SCRIPT}"
        --patches "${DKC3_RUNTIME_PATCH_DIR}"
        --snesrecomp "${SNESRECOMP_ROOT}"
        --out "${DKC3_RUNTIME_PATCHED_DIR}"
    RESULT_VARIABLE dkc3_runtime_patch_result
    OUTPUT_VARIABLE dkc3_runtime_patch_output
    ERROR_VARIABLE dkc3_runtime_patch_error)
if(NOT dkc3_runtime_patch_result EQUAL 0)
    message(FATAL_ERROR
        "DKC3 runtime adaptation failed; review cmake/runtime-patches against the pinned snesrecomp:\n${dkc3_runtime_patch_error}")
endif()

file(STRINGS "${DKC3_RUNTIME_PATCHED_DIR}/manifest.txt" dkc3_runtime_manifest)
set(DKC3_RUNTIME_ORIGINAL_SOURCES)
set(DKC3_RUNTIME_PATCHED_SOURCES)
foreach(dkc3_manifest_line IN LISTS dkc3_runtime_manifest)
    string(REPLACE "\t" ";" dkc3_manifest_pair "${dkc3_manifest_line}")
    list(GET dkc3_manifest_pair 0 dkc3_original)
    list(GET dkc3_manifest_pair 1 dkc3_patched)
    list(APPEND DKC3_RUNTIME_ORIGINAL_SOURCES "${dkc3_original}")
    list(APPEND DKC3_RUNTIME_PATCHED_SOURCES "${dkc3_patched}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${dkc3_original}")
    get_filename_component(dkc3_patched_name "${dkc3_patched}" NAME)
    if(dkc3_patched_name STREQUAL "ppu.c")
        set(DKC3_PATCHED_PPU "${dkc3_patched}")
    endif()
endforeach()
if(NOT DEFINED DKC3_PATCHED_PPU)
    message(FATAL_ERROR "DKC3 runtime adaptation did not produce ppu.c")
endif()
list(LENGTH DKC3_RUNTIME_PATCHED_SOURCES dkc3_runtime_patched_count)
message(STATUS "DKC3: ${dkc3_runtime_patched_count} runtime sources adapted in ${DKC3_RUNTIME_PATCHED_DIR}")

function(dkc3_use_patched_runtime_sources list_name)
    set(sources ${${list_name}})
    foreach(original IN LISTS DKC3_RUNTIME_ORIGINAL_SOURCES)
        list(FIND sources "${original}" index)
        if(index EQUAL -1)
            message(FATAL_ERROR
                "DKC3 runtime adaptation targets ${original}, which is not in ${list_name}")
        endif()
        list(REMOVE_ITEM sources "${original}")
    endforeach()
    list(APPEND sources ${DKC3_RUNTIME_PATCHED_SOURCES})
    set(${list_name} ${sources} PARENT_SCOPE)
endfunction()
