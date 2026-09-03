cmake_minimum_required(VERSION 3.20)

project(dkc3_port
    VERSION 0.0.1
    DESCRIPTION "Native PC recompilation of Donkey Kong Country 3"
    LANGUAGES C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(DKC3_ROM "" CACHE FILEPATH
    "Private path to a headerless DKC3 USA (En,Fr) ROM")
set(DKC3_DESKTOP_ICON "" CACHE FILEPATH
    "Optional private ICO embedded in the Windows desktop executable")
option(DKC3_BUILD_SNESRECOMP
    "Build the snesrecomp headless and desktop runners" ON)
option(DKC3_FETCH_SDL2
    "Fetch the pinned SDL2 source when no system package is available" ON)

function(dkc3_enable_project_warnings target_name)
    if(MSVC)
        # Visual Studio otherwise sends every source in a target through one
        # serial cl.exe invocation. Generated recompilation units are large,
        # so allow the compiler to process them concurrently.
        target_compile_options(${target_name} PRIVATE /W4 /MP)
        target_compile_definitions(${target_name} PRIVATE
            _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(dkc3_enable_release_optimization target_name)
    if(MSVC)
        # MSVC has no /O3 level; /O2 is its maximum supported speed preset.
        target_compile_options(${target_name} PRIVATE
            "$<$<CONFIG:Release>:/O2>")
    else()
        target_compile_options(${target_name} PRIVATE
            "$<$<CONFIG:Release>:-O3>")
    endif()
endfunction()

if(DKC3_BUILD_SNESRECOMP)
    if(NOT DEFINED SNESRECOMP_ROOT)
        set(SNESRECOMP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp)
    endif()
    set(DKC3_SNESRECOMP_GEN_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/snesrecomp" CACHE PATH
        "Directory containing private generated snesrecomp C sources")
    if(NOT EXISTS "${SNESRECOMP_ROOT}/runner/runner.cmake")
        message(FATAL_ERROR
            "snesrecomp is not initialized; run git submodule update --init --recursive")
    endif()
    include(${SNESRECOMP_ROOT}/runner/runner.cmake)
    # SDL2 and recomp-ui are portable dependencies shared by the launcher and
    # the gameplay host on Windows, Linux, and macOS. Prefer a package config
    # (vcpkg/Homebrew commonly provide one), then accept a distribution module.
    find_package(SDL2 CONFIG QUIET)
    if(NOT TARGET SDL2::SDL2)
        if(NOT DKC3_FETCH_SDL2)
            message(FATAL_ERROR
                "SDL2 was not found. Install SDL2 or configure with DKC3_FETCH_SDL2=ON")
        endif()
        include(FetchContent)
        set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-2.30.9
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(SDL2)
    endif()
    set(SNESRECOMP_SDL_BACKEND SDL2)
    include(${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui/recomp_ui.cmake)
    file(GLOB DKC3_SNESRECOMP_GEN_SOURCES CONFIGURE_DEPENDS
        "${DKC3_SNESRECOMP_GEN_DIR}/*.c")
    if(NOT DKC3_SNESRECOMP_GEN_SOURCES)
        message(FATAL_ERROR
            "No private generated sources found; run scripts/generate_snesrecomp.py first")
    endif()
    # These project-owned hosts have no SDL dependency or event thread. Keep
    # the portable snesrecomp runtime while providing their small host adapters.
    list(REMOVE_ITEM SNESRECOMP_RUNNER_SOURCES
        "${SNESRECOMP_ROOT}/runner/src/keybinds.c"
        "${SNESRECOMP_ROOT}/runner/src/host_report.c")
    add_executable(dkc3_snesrecomp_headless
        ${SNESRECOMP_RUNNER_SOURCES}
        ${DKC3_SNESRECOMP_GEN_SOURCES}
        runner/dkc3_game.c
        runner/dkc3_video.c
        runner/headless_host.c
        runner/input_playback.c
        runner/verified_rom.c
        runner/headless_main.c)
    target_include_directories(dkc3_snesrecomp_headless PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_compile_definitions(dkc3_snesrecomp_headless PRIVATE
        SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
        SNESRECOMP_REVERSE_DEBUG=0
        SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1
        SYSTEM_VOLUME_MIXER_AVAILABLE=0)
    target_link_libraries(dkc3_snesrecomp_headless PRIVATE
        ${SNESRECOMP_RUNNER_LIBRARIES})
    dkc3_enable_project_warnings(dkc3_snesrecomp_headless)
    dkc3_enable_release_optimization(dkc3_snesrecomp_headless)

    if(WIN32)
        set(DKC3_DESKTOP_ICON_RESOURCE "")
        if(DKC3_DESKTOP_ICON)
            if(NOT EXISTS "${DKC3_DESKTOP_ICON}")
                message(FATAL_ERROR
                    "DKC3_DESKTOP_ICON does not exist: ${DKC3_DESKTOP_ICON}")
            endif()
            enable_language(RC)
            get_filename_component(DKC3_DESKTOP_ICON_ABSOLUTE
                "${DKC3_DESKTOP_ICON}" ABSOLUTE)
            file(TO_CMAKE_PATH "${DKC3_DESKTOP_ICON_ABSOLUTE}"
                DKC3_DESKTOP_ICON_RC)
            configure_file(
                runner/desktop_icon.rc.in
                "${CMAKE_CURRENT_BINARY_DIR}/dkc3_desktop_icon.rc"
                @ONLY)
            set(DKC3_DESKTOP_ICON_RESOURCE
                "${CMAKE_CURRENT_BINARY_DIR}/dkc3_desktop_icon.rc")
        endif()

        # The Win32 host is carried over from DKC2Recomp unchanged apart from
        # the game adapter. It has not been built on this project's macOS
        # bring-up machine.
        add_executable(dkc3_snesrecomp_desktop WIN32
            ${DKC3_DESKTOP_ICON_RESOURCE}
            ${SNESRECOMP_RUNNER_SOURCES}
            ${DKC3_SNESRECOMP_GEN_SOURCES}
            runner/dkc3_game.c
            runner/dkc3_video.c
            runner/diagnostics.c
            runner/headless_host.c
            runner/verified_rom.c
            runner/desktop_fps.c
            runner/desktop_filter.c
            runner/desktop_input.c
            runner/input_recording.c
            runner/desktop_launcher.c
            runner/desktop_overlay.cpp
            runner/desktop_overlay_model.c
            runner/desktop_perf.c
            runner/desktop_present.c
            runner/desktop_present_gl.c
            runner/desktop_vsync.c
            runner/desktop_rewind.c
            runner/desktop_viewport.c
            runner/desktop_main.c
            ${SNESRECOMP_ROOT}/runner/src/host_report.c)
        target_include_directories(dkc3_snesrecomp_desktop PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/runner
            ${CMAKE_CURRENT_SOURCE_DIR}/recomp
            ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
        target_compile_definitions(dkc3_snesrecomp_desktop PRIVATE
            DKC3_RELEASE_VERSION="${PROJECT_VERSION}"
            SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
            SNESRECOMP_REVERSE_DEBUG=0
            SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1
            SYSTEM_VOLUME_MIXER_AVAILABLE=0
            DKC3_REAL_HOST_REPORT=1)
        target_link_libraries(dkc3_snesrecomp_desktop PRIVATE
            ${SNESRECOMP_RUNNER_LIBRARIES}
            SDL2::SDL2
            user32
            comdlg32
            gdi32
            opengl32
            shell32
            winmm
            xinput9_1_0)
        set_target_properties(dkc3_snesrecomp_desktop PROPERTIES
            OUTPUT_NAME DKC3Recomp)
        recomp_target_launcher_ui(dkc3_snesrecomp_desktop
            CONSOLE snes
            BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)
        dkc3_enable_project_warnings(dkc3_snesrecomp_desktop)
        dkc3_enable_release_optimization(dkc3_snesrecomp_desktop)
    endif()

    # Cross-platform playable host: SDL2 window and OpenGL context, the
    # shared recomp-ui launcher and overlay, queued audio, and on macOS the
    # display-link pacing carried over from DKC2Recomp.
    add_executable(dkc3_snesrecomp_sdl
        ${SNESRECOMP_RUNNER_SOURCES}
        ${DKC3_SNESRECOMP_GEN_SOURCES}
        runner/dkc3_game.c
        runner/dkc3_video.c
        runner/diagnostics.c
        runner/headless_host.c
        runner/verified_rom.c
        runner/desktop_filter.c
        runner/desktop_fps.c
        runner/desktop_input.c
        runner/input_recording.c
        runner/desktop_launcher.c
        runner/desktop_overlay.cpp
        runner/desktop_overlay_model.c
        runner/desktop_present_sdl.c
        runner/desktop_vsync.c
        runner/desktop_rewind.c
        runner/desktop_viewport.c
        runner/desktop_pacer.c
        runner/desktop_audio_rate.c
        runner/sdl_main.c
        ${SNESRECOMP_ROOT}/runner/src/host_report.c)
    if(APPLE)
        enable_language(OBJC)
        target_sources(dkc3_snesrecomp_sdl PRIVATE
            runner/macos_host.m
            runner/macos_display_link.m
            assets/macos/DKC3Recomp.icns)
        set_source_files_properties(assets/macos/DKC3Recomp.icns PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
    endif()
    target_include_directories(dkc3_snesrecomp_sdl PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp
        ${SNESRECOMP_RUNNER_INCLUDE_DIRS})
    target_compile_definitions(dkc3_snesrecomp_sdl PRIVATE
        DKC3_RELEASE_VERSION="${PROJECT_VERSION}"
        SNESRECOMP_TRACE=$<BOOL:${SNESRECOMP_ENABLE_TRACE}>
        SNESRECOMP_REVERSE_DEBUG=0
        SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1
        SYSTEM_VOLUME_MIXER_AVAILABLE=0
        SDL_MAIN_HANDLED
        DKC3_REAL_HOST_REPORT=1)
    target_link_libraries(dkc3_snesrecomp_sdl PRIVATE
        ${SNESRECOMP_RUNNER_LIBRARIES}
        SDL2::SDL2)
    if(APPLE)
        target_link_libraries(dkc3_snesrecomp_sdl PRIVATE
            "-framework AppKit"
            "-framework QuartzCore")
    endif()
    if(UNIX AND NOT APPLE)
        target_link_libraries(dkc3_snesrecomp_sdl PRIVATE m)
    endif()
    if(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(dkc3_snesrecomp_sdl PRIVATE Threads::Threads)
    endif()
    if(WIN32)
        set_target_properties(dkc3_snesrecomp_sdl PROPERTIES
            OUTPUT_NAME DKC3RecompSDL)
    else()
        set_target_properties(dkc3_snesrecomp_sdl PROPERTIES
            OUTPUT_NAME DKC3Recomp)
    endif()
    if(APPLE)
        set_target_properties(dkc3_snesrecomp_sdl PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "DKC3Recomp"
            MACOSX_BUNDLE_ICON_FILE "DKC3Recomp.icns"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.flat2vr.dkc3recomp"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
    endif()
    recomp_target_launcher_ui(dkc3_snesrecomp_sdl
        CONSOLE snes
        BOXART ${CMAKE_SOURCE_DIR}/recomp/launcher/boxart.tga)
    dkc3_enable_project_warnings(dkc3_snesrecomp_sdl)
    dkc3_enable_release_optimization(dkc3_snesrecomp_sdl)

    if(BUILD_TESTING AND DKC3_ROM)
        add_test(NAME supplied_rom_headless_boot
            COMMAND dkc3_snesrecomp_headless "${DKC3_ROM}" 600)
        set_tests_properties(supplied_rom_headless_boot PROPERTIES
            LABELS "private;integration;native;headless"
            PASS_REGULAR_EXPRESSION "result=completed frames=600"
            TIMEOUT 120)
    endif()

    if(BUILD_TESTING AND APPLE AND DKC3_ROM)
        add_test(NAME supplied_rom_macos_app_smoke
            COMMAND ${CMAKE_COMMAND} -E env
                DKC3_DESKTOP_TEST_FRAMES=180
                DKC3_DESKTOP_TEST_HIDDEN=1
                DKC3_DESKTOP_TEST_OVERLAY=1
                DKC3_DESKTOP_TEST_REWIND=1
                DKC3_DESKTOP_TEST_FASTFORWARD=1
                DKC3_DESKTOP_DISABLE_SRAM=1
                DKC3_USER_DIR=${CMAKE_CURRENT_BINARY_DIR}/macos-test-user
                SNESRECOMP_NO_LAUNCHER=1
                "$<TARGET_FILE:dkc3_snesrecomp_sdl>"
                "${DKC3_ROM}")
        set_tests_properties(supplied_rom_macos_app_smoke PROPERTIES
            LABELS "private;integration;native;desktop;sdl;macos"
            PASS_REGULAR_EXPRESSION
                "result=desktop_completed frames=180.*host=sdl2"
            FAIL_REGULAR_EXPRESSION
                "shader failed to compile;Unable to initialize the in-game overlay"
            TIMEOUT 45)

        add_test(NAME supplied_rom_macos_16x10_smoke
            COMMAND ${CMAKE_COMMAND} -E env
                DKC3_DESKTOP_TEST_FRAMES=90
                DKC3_DESKTOP_TEST_HIDDEN=1
                DKC3_DESKTOP_DISABLE_SRAM=1
                DKC3_USER_DIR=${CMAKE_CURRENT_BINARY_DIR}/macos-test-user-16x10
                DKC3_ASPECT=16:10
                SNESRECOMP_NO_LAUNCHER=1
                "$<TARGET_FILE:dkc3_snesrecomp_sdl>"
                "${DKC3_ROM}")
        set_tests_properties(supplied_rom_macos_16x10_smoke PROPERTIES
            LABELS "private;integration;native;desktop;macos;widescreen"
            PASS_REGULAR_EXPRESSION
                "result=desktop_completed frames=90.*host=sdl2"
            TIMEOUT 30)

        add_test(NAME supplied_rom_macos_quick_state_smoke
            COMMAND ${CMAKE_COMMAND} -E env
                DKC3_DESKTOP_TEST_FRAMES=90
                DKC3_DESKTOP_TEST_HIDDEN=1
                DKC3_DESKTOP_TEST_SAVELOAD=1
                DKC3_DESKTOP_DISABLE_SRAM=1
                DKC3_USER_DIR=${CMAKE_CURRENT_BINARY_DIR}/macos-test-user-quick-state
                SNESRECOMP_NO_LAUNCHER=1
                "$<TARGET_FILE:dkc3_snesrecomp_sdl>"
                "${DKC3_ROM}")
        set_tests_properties(supplied_rom_macos_quick_state_smoke PROPERTIES
            LABELS "private;integration;native;desktop;macos;save-state"
            PASS_REGULAR_EXPRESSION
                "result=desktop_completed frames=90.*save_load=passed.*host=sdl2"
            TIMEOUT 30)
    endif()
endif()

if(BUILD_TESTING)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME dkc3_pacing_log_tool
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_analyze_pacing_log.py)
        set_tests_properties(dkc3_pacing_log_tool PROPERTIES
            LABELS "unit;tooling;desktop;performance;portable")
        add_test(NAME dkc3_ingest_tool
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ingest_dkc3_disasm.py)
        set_tests_properties(dkc3_ingest_tool PROPERTIES
            LABELS "unit;tooling;recomp;portable")
    endif()

    add_executable(test_desktop_input
        tests/test_desktop_input.c
        runner/desktop_input.c)
    target_include_directories(test_desktop_input PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui/src)
    dkc3_enable_project_warnings(test_desktop_input)
    add_test(NAME desktop_input COMMAND test_desktop_input)
    set_tests_properties(desktop_input PROPERTIES
        LABELS "unit;desktop;input")

    add_executable(test_input_playback
        tests/test_input_playback.c
        runner/input_playback.c)
    target_include_directories(test_input_playback PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner)
    dkc3_enable_project_warnings(test_input_playback)
    add_test(NAME input_playback
        COMMAND test_input_playback
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/input_playback_valid.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/input_playback_invalid.txt)
    set_tests_properties(input_playback PROPERTIES
        LABELS "unit;native;input")

    add_executable(test_input_recording
        tests/test_input_recording.c
        runner/input_recording.c)
    target_include_directories(test_input_recording PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner)
    dkc3_enable_project_warnings(test_input_recording)
    add_test(NAME input_recording
        COMMAND test_input_recording
            ${CMAKE_CURRENT_BINARY_DIR}/input-recording-test.input
            ${CMAKE_CURRENT_BINARY_DIR}/missing-recording-dir/test.input)
    set_tests_properties(input_recording PROPERTIES
        LABELS "unit;native;input;diagnostics")

    add_executable(test_launcher_defaults
        tests/test_launcher_defaults.c
        recomp-ui/src/common/launcher_model.c
        recomp-ui/src/common/crc32.c
        recomp-ui/src/common/sha256.c
        recomp-ui/src/common/sha1.c
        recomp-ui/src/common/ips_patch.c
        recomp-ui/src/consoles/psx/memcard_format.c)
    target_include_directories(test_launcher_defaults PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui/src
        ${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui/src/common)
    dkc3_enable_project_warnings(test_launcher_defaults)
    add_test(NAME launcher_defaults COMMAND test_launcher_defaults)
    set_tests_properties(launcher_defaults PROPERTIES
        LABELS "unit;desktop;launcher")

    foreach(module IN ITEMS rewind overlay_model fps pacer audio_rate perf viewport)
        add_executable(test_desktop_${module}
            tests/test_desktop_${module}.c
            runner/desktop_${module}.c)
        target_include_directories(test_desktop_${module} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/runner)
        dkc3_enable_project_warnings(test_desktop_${module})
        add_test(NAME desktop_${module} COMMAND test_desktop_${module})
        set_tests_properties(desktop_${module} PROPERTIES
            LABELS "unit;desktop")
    endforeach()

    if(WIN32)
        add_executable(test_desktop_present
            tests/test_desktop_present.c
            runner/desktop_filter.c
            runner/desktop_present.c
            runner/desktop_present_gl.c
            runner/desktop_vsync.c
            runner/desktop_viewport.c
            snesrecomp/runner/src/snes/color_lut.c)
        target_include_directories(test_desktop_present PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/runner
            ${CMAKE_CURRENT_SOURCE_DIR}/snesrecomp/runner/src)
        target_link_libraries(test_desktop_present PRIVATE
            gdi32 user32 opengl32)
        dkc3_enable_project_warnings(test_desktop_present)
        add_test(NAME desktop_present COMMAND test_desktop_present)
        set_tests_properties(desktop_present PROPERTIES
            LABELS "unit;desktop;video")
    endif()

    # The snesrecomp submodule's own interpreter regressions are not wired in:
    # at the pinned revision its interp816 test fails two page-cross cycle
    # checks (LDA abs,X / abs,Y expect 6 cycles, the runtime counts 5) and
    # its bridge test calls a tier-2 capture setter the runtime no longer
    # declares. Both are upstream test-versus-runtime drift, recorded in
    # docs/BRINGUP.md, and neither affects the game's behaviour here.
endif()
