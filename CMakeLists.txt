cmake_minimum_required(VERSION 3.24)
project(DisruptorRecomp C CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)

# A Linux release runs on systems whose C++ library is older than the compiler's: glibc is all it asks of them.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options(-static-libstdc++ -static-libgcc)
endif()
# libGL.so.1 is on every Linux system that has OpenGL, GLVND's libOpenGL.so.0 only where its own package is installed.
set(OpenGL_GL_PREFERENCE LEGACY)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "DisruptorRecomp supports 64-bit Windows builds only. "
        "Use an x64 Visual Studio developer environment.")
endif()

set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to the pinned PSXRecomp checkout")

option(DISRUPTOR_DEV_MENU
    "Build the in-game Disruptor settings menu"
    ON)
option(DISRUPTOR_FRAME_INTERPOLATION
    "Build the experimental presentation-only frame interpolator"
    ON)

set(DISRUPTOR_EXTRA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/backdrop_tiles.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/billboard_aspect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/session/capsule.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/session/cheats.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/input/control_probe.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/session/diagnostics.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/far_rendering.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/frame/frame_rate.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/host.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings/saved.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings/user_folder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/session/intro_skip.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/language/language.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/language/language_briefing.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/language/language_disc.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/language/language_hints.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/language/language_movie_sound.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/language/language_source.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/session/restart.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/input/baton_choice.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/input/mouse_aim.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/input/name_entry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/input/selection_wheel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/precision_routes.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/render_arena.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/grown_quad.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/hud_widgets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/mover_motion.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/sprite_depth.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/input/vertical_camera.cpp")

# The project deliberately uses the redistributable OpenBIOS backend only.
set(PSXRECOMP_BIOS_STEMS "OpenBIOS" CACHE STRING "" FORCE)
set(PSX_RECOMP_UI OFF CACHE BOOL "" FORCE)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src")
# The framework's settings reader takes the game's section of settings.toml from here, in every target alike.
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings")
add_compile_definitions(PSX_HAS_GAME_USER_SETTINGS=1)
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

# The in-game menu and the launcher both draw with ImGui.
include(FetchContent)
FetchContent_Declare(disruptor_imgui
    URL
        "https://github.com/ocornut/imgui/archive/refs/tags/v1.92.6.tar.gz"
    URL_HASH
        "SHA256=5b17c01f69545bde732b14936d89ce0f508adb83e8b56fa82448371845172bc3"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(disruptor_imgui)

if(DISRUPTOR_DEV_MENU)

    if(PSX_SDL3)
        set(DISRUPTOR_IMGUI_SDL_BACKEND
            "${disruptor_imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp")
    else()
        set(DISRUPTOR_IMGUI_SDL_BACKEND
            "${disruptor_imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp")
    endif()

    add_library(disruptor-imgui STATIC
        "${disruptor_imgui_SOURCE_DIR}/imgui.cpp"
        "${disruptor_imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${disruptor_imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${disruptor_imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${DISRUPTOR_IMGUI_SDL_BACKEND}"
        "${disruptor_imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
    target_include_directories(disruptor-imgui PUBLIC
        "${disruptor_imgui_SOURCE_DIR}"
        "${disruptor_imgui_SOURCE_DIR}/backends")
    target_link_libraries(disruptor-imgui PRIVATE ${PSX_SDL_LIBRARIES})
    target_compile_features(disruptor-imgui PUBLIC cxx_std_20)
    if(WIN32)
        target_compile_definitions(
            disruptor-imgui PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>")
    endif()

    list(APPEND DISRUPTOR_EXTRA_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings/options.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/preferences.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/rows.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/tab_controls.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/tab_display.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/tab_enhancements.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/tab_experimental.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/tab_debug.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/tab_cheats.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/tab_system.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/tab_language.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/menu.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/selection_wheel.cpp")
    if(DISRUPTOR_FRAME_INTERPOLATION)
        list(APPEND DISRUPTOR_EXTRA_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/game/frame/present_rate.cpp")
    endif()
    set(DISRUPTOR_WINDOW_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/launcher/assets/app-64.bmp")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/window_icon.cpp"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${DISRUPTOR_WINDOW_ICON}" "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/window_icon.cpp"
            -DNAME=disruptor_window_icon_bmp -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_bytes.cmake"
        DEPENDS "${DISRUPTOR_WINDOW_ICON}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_bytes.cmake"
        VERBATIM)
    list(APPEND DISRUPTOR_EXTRA_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/window_icon.cpp")
endif()

file(GLOB GAME_GENERATED_FULL CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_002.24.code_full.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_002.24.code_full_*.c")
set(GAME_GENERATED_DISPATCH
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLUS_002.24.code_dispatch.c")
set(DISRUPTOR_STATIC_OVERLAY_DEFAULT OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c")
    set(DISRUPTOR_STATIC_OVERLAY_DEFAULT ON)
endif()
option(DISRUPTOR_USE_CAPTURED_STATIC_OVERLAY
    "Link generated/overlays_static.c when a private captured-code build has produced it"
    ${DISRUPTOR_STATIC_OVERLAY_DEFAULT})
set(GAME_OVERLAY_STATIC "")
if(DISRUPTOR_USE_CAPTURED_STATIC_OVERLAY)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c")
        message(FATAL_ERROR
            "DISRUPTOR_USE_CAPTURED_STATIC_OVERLAY is enabled, but the private "
            "generated/overlays_static.c file does not exist")
    endif()
    set(GAME_OVERLAY_STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/overlays_static.c")
endif()

psxrecomp_add_runtime_target(psx-runtime
    GAME_GENERATED_FULL_C ${GAME_GENERATED_FULL}
    GAME_GENERATED_DISPATCH_C "${GAME_GENERATED_DISPATCH}"
    GAME_OVERLAY_STATIC_C "${GAME_OVERLAY_STATIC}"
    EXTRAS_SOURCES
        ${DISRUPTOR_EXTRA_SOURCES}
    DEBUG_PORT 4624
    WINDOW_TITLE "Disruptor Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    EXE_NAME "DisruptorRecompiled"
)
target_compile_definitions(psx-runtime PRIVATE
    PSX_HAS_GAME_HOST=1)
if(NOT DISRUPTOR_FRAME_INTERPOLATION)
    target_compile_definitions(psx-runtime PRIVATE
        # Keep the experimental sources available, but compile out frame
        # blending, temporal capture and the high-refresh presenter by default.
        PSX_DISABLE_FRAME_INTERPOLATION=1)
endif()
if(DISRUPTOR_DEV_MENU)
    target_compile_definitions(psx-runtime PRIVATE PSX_FRAME_INTERPOLATION_GEOMETRY_DEFAULT=1)
    target_link_libraries(psx-runtime PRIVATE disruptor-imgui)
endif()
if(WIN32)
    target_compile_definitions(
        psx-runtime PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:NOMINMAX>")
endif()
if(MSVC)
    # audio_trace.c is the sole runtime source using C11 atomics. Keep the
    # generated C in MSVC's extension-compatible C mode: its static tables use
    # address constants that strict /std:c11 rejects.
    set_property(SOURCE "${PSXRECOMP_ROOT}/runtime/src/audio_trace.c"
        APPEND PROPERTY COMPILE_OPTIONS
        "/std:c11" "/experimental:c11atomics")
endif()

if(WIN32)
    target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/game/icon.rc")
    # UTF-8 as the game's code page: a path in settings.toml may hold letters the system's own page lacks.
    if(MINGW)
        target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/game/disruptor.rc")
    else()
        target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/game/disruptor.manifest")
    endif()
endif()

include(CTest)
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(src/launcher)
endif()
if(BUILD_TESTING)
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DisruptorTests.cmake")

    disruptor_contract(backdrop_tiles RETAIL)
    disruptor_contract(depth24_sync)
    disruptor_contract(geometry_provenance)
    disruptor_contract(phase_a)
    disruptor_contract(perspective_texture)
    disruptor_contract(pinned_corner)
    disruptor_contract(dev_menu)
    disruptor_contract(loaded_state)
    disruptor_contract(save_point)
    disruptor_contract(cheats)
    disruptor_contract(weapon_selection RETAIL)
    disruptor_contract(selection_wheel RETAIL)
    disruptor_contract(vertical_camera)
    disruptor_contract(far_rendering)
    disruptor_contract(frame_kind)
    disruptor_contract(temporal_presenter)
    disruptor_contract(menu_wide)
    disruptor_contract(hud_scale)
    disruptor_contract(hud_widget)
    disruptor_contract(name_entry RETAIL)
    disruptor_contract(fast_forward_key)
    disruptor_contract(grown_quad RETAIL)
    disruptor_contract(mover_steps RETAIL)
    disruptor_contract(sprite_range RETAIL)
    disruptor_contract(intro_skip RETAIL)
    disruptor_contract(language RETAIL)
    disruptor_contract(language_disc RETAIL)
    disruptor_contract(frame_rate RETAIL)
    disruptor_contract(widescreen_cull RETAIL)
    disruptor_contract(billboard_aspect)
    disruptor_contract(launcher)
    disruptor_contract(release_text)
    disruptor_contract(capsule)
    disruptor_contract(gap_filling)
    disruptor_contract(world_depth)
    disruptor_contract(world_texel)
    disruptor_contract(near_face)
    disruptor_contract(smooth_turn)
    disruptor_contract(ambient_occlusion)
    disruptor_contract(smooth_fade)
    disruptor_contract(antialiasing)
    disruptor_contract(debug_tab)
    disruptor_contract(diagnostics)
    disruptor_contract(instruction_hooks)
    disruptor_contract(state_key)
    disruptor_contract(test_registration)

    add_test(NAME disruptor_framework_tool
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contracts/test_framework_tool.py")
    # The contracts and some unit tests read framework files from the checkout: it must hold what framework/ says.
    file(REAL_PATH "${PSXRECOMP_ROOT}" framework_checkout)
    file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" framework_expected)
    if(NOT framework_checkout STREQUAL framework_expected)
        message(FATAL_ERROR "The tests read the framework from ${framework_expected}, not ${framework_checkout}")
    endif()
    add_test(NAME disruptor_framework_in_step
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/framework.py" check --framework "${PSXRECOMP_ROOT}")
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    disruptor_unit_test(disruptor_user_settings_persistence tests/unit/user_settings_persistence_test.cpp
        TARGET disruptor-user-settings-test
        SOURCES
            "${PSXRECOMP_ROOT}/recompiler/src/config_loader.cpp"
            "${PSXRECOMP_ROOT}/recompiler/src/ps1_exe_parser.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings/saved.cpp"
        INCLUDES
            "${PSXRECOMP_ROOT}/recompiler/src"
            "${PSXRECOMP_ROOT}/recompiler/include"
            "${PSXRECOMP_ROOT}/recompiler/lib/fmt/include"
            "${PSXRECOMP_ROOT}/recompiler/lib/toml11"
        DEFINITIONS
            FMT_HEADER_ONLY=1
            "$<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>"
            "$<$<CXX_COMPILER_ID:MSVC>:WIN32_LEAN_AND_MEAN>")

    disruptor_unit_test(disruptor_game_config tests/unit/game_config_test.cpp
        TARGET disruptor-game-config-test
        SOURCES
            "${PSXRECOMP_ROOT}/recompiler/src/config_loader.cpp"
            "${PSXRECOMP_ROOT}/recompiler/src/ps1_exe_parser.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings/saved.cpp"
        INCLUDES
            "${PSXRECOMP_ROOT}/recompiler/src"
            "${PSXRECOMP_ROOT}/recompiler/include"
            "${PSXRECOMP_ROOT}/recompiler/lib/fmt/include"
            "${PSXRECOMP_ROOT}/recompiler/lib/toml11"
        DEFINITIONS
            FMT_HEADER_ONLY=1
            "$<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>"
            "$<$<CXX_COMPILER_ID:MSVC>:WIN32_LEAN_AND_MEAN>")
    target_compile_definitions(disruptor-game-config-test PRIVATE "DISRUPTOR_SOURCE_DIR=R\"(${PROJECT_SOURCE_DIR})\"")

    disruptor_unit_test(disruptor_user_folder tests/unit/user_folder_test.cpp RUNTIME_HEADERS
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings/user_folder.cpp")

    disruptor_unit_test(disruptor_mouse_aim tests/unit/disruptor_mouse_aim_test.cpp
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings/user_folder.cpp"
        INCLUDES tests/support/stubs "${PSXRECOMP_ROOT}/runtime/include"
        DEFINITIONS PSX_SDL3=1)
    disruptor_unit_test(disruptor_selection_wheel tests/unit/disruptor_selection_wheel_test.cpp
        INCLUDES tests/support/stubs DEFINITIONS PSX_SDL3=1)
    set_tests_properties(disruptor_mouse_aim PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_VERTICAL_LOOK=;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_mouse_aim_replay
        COMMAND disruptor-mouse-aim-test --replay)
    set_tests_properties(disruptor_mouse_aim_replay PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_CAPSULE_REPLAY=capsule;PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_VERTICAL_LOOK=;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_mouse_aim_registration
        COMMAND disruptor-mouse-aim-test --registration)
    set_tests_properties(disruptor_mouse_aim_registration PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=1;PSX_DISRUPTOR_VERTICAL_LOOK=;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_vertical_look_registration
        COMMAND disruptor-mouse-aim-test --registration)
    set_tests_properties(disruptor_vertical_look_registration PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_VERTICAL_LOOK=1;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_modern_controls_registration
        COMMAND disruptor-mouse-aim-test --registration)
    set_tests_properties(disruptor_modern_controls_registration PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_VERTICAL_LOOK=;PSX_DISRUPTOR_MODERN_CONTROLS=1;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=")

    add_test(NAME disruptor_high_precision_camera_registration
        COMMAND disruptor-mouse-aim-test --registration)
    set_tests_properties(disruptor_high_precision_camera_registration PROPERTIES
        ENVIRONMENT "PSX_DISRUPTOR_MOUSE_AIM=;PSX_DISRUPTOR_VERTICAL_LOOK=;PSX_DISRUPTOR_MODERN_CONTROLS=;PSX_DISRUPTOR_HIGH_PRECISION_CAMERA=1")

    disruptor_unit_test(disruptor_diagnostics tests/unit/disruptor_diagnostics_test.cpp
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/game/session/diagnostics.cpp"
        INCLUDES tests/support/stubs
        DEFINITIONS PSX_SDL3=1)
    set_tests_properties(disruptor_diagnostics PROPERTIES ENVIRONMENT "PSX_DISRUPTOR_DETAILED_LOGGING=")
    add_test(NAME disruptor_diagnostics_forced COMMAND disruptor-diagnostics-test --forced)
    set_tests_properties(disruptor_diagnostics_forced PROPERTIES ENVIRONMENT "PSX_DISRUPTOR_DETAILED_LOGGING=1")
    add_test(NAME disruptor_diagnostics_refused COMMAND disruptor-diagnostics-test --refused)
    set_tests_properties(disruptor_diagnostics_refused PROPERTIES ENVIRONMENT "PSX_DISRUPTOR_DETAILED_LOGGING=0")

    disruptor_unit_test(disruptor_cheats tests/unit/disruptor_cheats_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_vertical_camera tests/unit/disruptor_vertical_camera_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_far_rendering tests/unit/disruptor_far_rendering_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_render_arena tests/unit/disruptor_render_arena_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_name_entry tests/unit/disruptor_name_entry_test.cpp RUNTIME_HEADERS)
disruptor_unit_test(disruptor_baton_choice tests/unit/disruptor_baton_choice_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_backdrop_tiles tests/unit/disruptor_backdrop_tiles_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_intro_skip tests/unit/disruptor_intro_skip_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_frame_rate tests/unit/disruptor_frame_rate_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(psx_cyc_overclock tests/unit/psx_cyc_overclock_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_language tests/unit/disruptor_language_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_language_hints tests/unit/disruptor_language_hints_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_language_movie_sound tests/unit/disruptor_language_movie_sound_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_language_briefing tests/unit/disruptor_language_briefing_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_language_disc tests/unit/disruptor_language_disc_test.cpp)
    disruptor_unit_test(disruptor_launcher_menu_art tests/unit/launcher_menu_art_test.cpp
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/launcher/menu_art.cpp")
    disruptor_unit_test(disruptor_language_overlay tests/unit/disruptor_language_overlay_test.cpp
        INCLUDES
            "${PSXRECOMP_ROOT}/runtime/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME disruptor_language_overlay_tall
        COMMAND disruptor-language-overlay-test tall)
    disruptor_unit_test(disruptor_capsule tests/unit/disruptor_capsule_test.cpp INCLUDES src)
    disruptor_unit_test(disruptor_restart tests/unit/disruptor_restart_test.cpp RUNTIME_HEADERS
        ARGUMENTS run "${CMAKE_CURRENT_BINARY_DIR}/disruptor_restart_test.txt")
    disruptor_unit_test(disruptor_present_rate tests/unit/disruptor_present_rate_test.cpp
        INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/present_rate_fakes")
    disruptor_unit_test(disruptor_grown_quad tests/unit/disruptor_grown_quad_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_mover_steps tests/unit/disruptor_mover_steps_test.cpp)
    disruptor_unit_test(disruptor_sprite_depth tests/unit/disruptor_sprite_depth_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_billboard_aspect tests/unit/disruptor_billboard_aspect_test.cpp RUNTIME_HEADERS)

    disruptor_unit_test(disruptor_gpu_ws_frame_kind tests/unit/gpu_ws_frame_kind_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_ws_menu tests/unit/gpu_ws_menu_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_ws_hud_scale tests/unit/gpu_ws_hud_scale_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_ws_hud_widget tests/unit/gpu_ws_hud_widget_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_ws_tag_match tests/unit/gpu_ws_tag_match_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_temporal_sprite tests/unit/gpu_temporal_sprite_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_temporal_match tests/unit/gpu_temporal_match_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_temporal_mesh tests/unit/gpu_temporal_mesh_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_pinned_corner tests/unit/gpu_pinned_corner_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_turned_perspective tests/unit/gpu_turned_perspective_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_turn_clip tests/unit/gpu_turn_clip_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_world_depth tests/unit/gpu_world_depth_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_near_face tests/unit/gpu_near_face_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_gpu_world_texel tests/unit/gpu_world_texel_test.cpp RUNTIME_HEADERS
        INCLUDES "${PSXRECOMP_ROOT}/runtime/src")
    disruptor_unit_test(disruptor_save_point tests/unit/save_point_test.cpp RUNTIME_HEADERS)
    disruptor_unit_test(disruptor_presentation_yaw_math tests/unit/presentation_yaw_math_test.cpp)
    disruptor_unit_test(disruptor_gte_precision_store_pc tests/unit/gte_precision_store_pc_test.cpp RUNTIME_HEADERS
        SOURCES "${PSXRECOMP_ROOT}/runtime/src/gte.cpp"
        DEFINITIONS PSX_NO_DEBUG_TOOLS=1 PSX_GTE_REGISTER_TEST=1 PSX_ENABLE_BLOCK_CYCLES=1)

    disruptor_unit_test(host_pick_slot tests/unit/host_pick_slot_test.cpp)
    disruptor_unit_test(host_borderless tests/unit/host_borderless_test.cpp)
    add_test(NAME host_borderless_contract
        COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/contracts/test_borderless_contract.py")
endif()
