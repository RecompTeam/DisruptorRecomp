add_library(disruptor-disc-import STATIC disc_import.cpp)
target_compile_features(disruptor-disc-import PUBLIC cxx_std_20)
target_include_directories(disruptor-disc-import PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
    target_compile_definitions(disruptor-disc-import PUBLIC
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()
target_link_libraries(disruptor-disc-import PUBLIC $<$<BOOL:${WIN32}>:bcrypt>)

# settings.toml goes through the game's own reader and writer.
add_library(disruptor-launcher-config STATIC
    "${PSXRECOMP_ROOT}/recompiler/src/config_loader.cpp"
    "${PSXRECOMP_ROOT}/recompiler/src/ps1_exe_parser.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/settings/saved.cpp")
target_compile_features(disruptor-launcher-config PUBLIC cxx_std_20)
target_include_directories(disruptor-launcher-config PUBLIC
    "${PSXRECOMP_ROOT}/recompiler/src"
    "${PSXRECOMP_ROOT}/recompiler/include"
    "${PSXRECOMP_ROOT}/recompiler/lib/fmt/include"
    "${PSXRECOMP_ROOT}/recompiler/lib/toml11")
target_compile_definitions(disruptor-launcher-config PUBLIC
    FMT_HEADER_ONLY=1 NOMINMAX WIN32_LEAN_AND_MEAN)

# A language disc is checked with the game's own layout code.
disruptor_achievements_library(disruptor-achievements-launcher STATIC_RUNTIME)

add_library(disruptor-launcher-model STATIC
    achievements_account.cpp
    choices.cpp
    guide.cpp
    ini_lines.cpp
    key_bindings.cpp
    pad_bindings.cpp
    release_notes.cpp
    $<IF:$<BOOL:${WIN32}>,platform_windows.cpp,platform_posix.cpp>
    menu_art.cpp
    player_files.cpp
    state_stamp.cpp
    launcher_flow.cpp
    launcher_settings.cpp
    region_disc.cpp
    report.cpp
    startup_log.cpp
    "${PROJECT_SOURCE_DIR}/src/game/language/language_disc.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/settings/options.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/settings/user_folder.cpp")
target_include_directories(disruptor-launcher-model PUBLIC "${PROJECT_SOURCE_DIR}/src")
if(DISRUPTOR_DEV_TOOLS)
    target_compile_definitions(disruptor-launcher-model PUBLIC DISRUPTOR_DEV_TOOLS=1)
endif()
target_link_libraries(disruptor-launcher-model PUBLIC
    disruptor-disc-import disruptor-launcher-config disruptor-achievements-launcher
    $<$<BOOL:${WIN32}>:shell32 ole32 advapi32 user32>)
# A save state's stamp and header come from the framework's own definitions; the hash is made at build time.
target_include_directories(disruptor-launcher-model PRIVATE "${PSXRECOMP_ROOT}/runtime/include")
add_dependencies(disruptor-launcher-model psxrecomp_codegen_hash)

# What a report calls this build: a release's own version, the tag it was cut from, or the commits after it.
# A release is tagged only when it is published, after its build, and its Linux build has no git checkout.
if(PSX_GAME_VERSION)
    set(DISRUPTOR_BUILD_VERSION "v${PSX_GAME_VERSION}")
else()
    execute_process(COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE DISRUPTOR_BUILD_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE DISRUPTOR_DESCRIBE_FAILED)
    if(DISRUPTOR_DESCRIBE_FAILED OR NOT DISRUPTOR_BUILD_VERSION)
        set(DISRUPTOR_BUILD_VERSION "unknown")
    endif()
endif()
set_property(SOURCE report.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    "DISRUPTOR_BUILD_VERSION=\"${DISRUPTOR_BUILD_VERSION}\"")

# The window draws with ImGui through SDL3's renderer. Under MSVC the launcher has its own SDL3 with the window, its
# events, dialogs and the Direct3D 11 and software renderers alone, built on the static C runtime with the rest.
if(NOT PSX_SDL3)
    message(FATAL_ERROR "The launcher draws with SDL3: keep PSX_SDL_BACKEND at SDL3.")
endif()
if(MSVC)
    if(NOT sdl3_SOURCE_DIR)
        message(FATAL_ERROR "The launcher builds its own SDL3 from the fetched source: configure with "
                            "PSX_SDL3_FETCH on and without an installed SDL3.")
    endif()
    include(ExternalProject)
    set(DISRUPTOR_LAUNCHER_SDL "${CMAKE_BINARY_DIR}/launcher-sdl3")
    ExternalProject_Add(disruptor-launcher-sdl3-build
        SOURCE_DIR "${sdl3_SOURCE_DIR}"
        BINARY_DIR "${DISRUPTOR_LAUNCHER_SDL}/build"
        INSTALL_DIR "${DISRUPTOR_LAUNCHER_SDL}"
        CMAKE_ARGS
            "-DCMAKE_BUILD_TYPE=$<CONFIG>"
            "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
            "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
            -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>"
            -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
            -DCMAKE_DEBUG_POSTFIX=
            -DSDL_RENDER=ON -DSDL_AUDIO=OFF -DSDL_GPU=OFF -DSDL_CAMERA=OFF -DSDL_JOYSTICK=ON -DSDL_HAPTIC=OFF -DSDL_HIDAPI=OFF
            -DSDL_POWER=OFF -DSDL_SENSOR=OFF -DSDL_TRAY=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF
            -DSDL_RENDER_D3D=OFF -DSDL_RENDER_D3D12=OFF -DSDL_RENDER_GPU=OFF -DSDL_RENDER_VULKAN=OFF
        BUILD_BYPRODUCTS "${DISRUPTOR_LAUNCHER_SDL}/lib/SDL3-static.lib")
    file(MAKE_DIRECTORY "${DISRUPTOR_LAUNCHER_SDL}/include")
    add_library(disruptor-launcher-sdl3 STATIC IMPORTED)
    set_target_properties(disruptor-launcher-sdl3 PROPERTIES
        IMPORTED_LOCATION "${DISRUPTOR_LAUNCHER_SDL}/lib/SDL3-static.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${DISRUPTOR_LAUNCHER_SDL}/include"
        INTERFACE_LINK_LIBRARIES
            "kernel32;user32;gdi32;winmm;imm32;ole32;oleaut32;version;uuid;advapi32;setupapi;shell32")
    add_dependencies(disruptor-launcher-sdl3 disruptor-launcher-sdl3-build)
    set(DISRUPTOR_LAUNCHER_SDL_LIBRARY disruptor-launcher-sdl3)
else()
    set(DISRUPTOR_LAUNCHER_SDL_LIBRARY ${PSX_SDL_LIBRARIES})
endif()

# Velopack updates an installed copy in place. Its C library is built for MSVC and for glibc, x86_64 here.
if(MSVC)
    set(DISRUPTOR_VELOPACK_LIBRARY velopack_libc_win_x64_msvc.lib)
    set(DISRUPTOR_VELOPACK_LINKS ws2_32 bcrypt ntdll userenv oleaut32 ole32 shell32 advapi32 crypt32 secur32 ncrypt)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(DISRUPTOR_VELOPACK_LIBRARY velopack_libc_linux_x64_gnu.a)
    set(DISRUPTOR_VELOPACK_LINKS pthread dl m)
endif()
if(DISRUPTOR_VELOPACK_LIBRARY)
    FetchContent_Declare(velopack_libc
        URL "https://github.com/velopack/velopack/releases/download/1.2.161/velopack_libc_1.2.161.zip"
        URL_HASH "SHA256=31de8f2cf05a47bd5328a9f98b0c41aec1582156f6075f1058b26941f7f17c20"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(velopack_libc)
    add_library(disruptor-velopack STATIC IMPORTED)
    set_target_properties(disruptor-velopack PROPERTIES
        IMPORTED_LOCATION "${velopack_libc_SOURCE_DIR}/lib-static/${DISRUPTOR_VELOPACK_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${velopack_libc_SOURCE_DIR}/include"
        INTERFACE_LINK_LIBRARIES "${DISRUPTOR_VELOPACK_LINKS}")
endif()

# FreeType's light hinting draws small text sharper than stb_truetype. Built without its optional codecs.
foreach(codec ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${codec} ON CACHE BOOL "" FORCE)
endforeach()
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF) # this folder only: the launcher ships no DLL of its own
FetchContent_Declare(disruptor_freetype
    URL "https://github.com/freetype/freetype/archive/refs/tags/VER-2-15-0.tar.gz"
    URL_HASH "SHA256=87c7f1f8699b91e89f773f7dcfa505b044e1dae67f62a55d4e29c3afdcac4fb8"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(disruptor_freetype)

add_library(disruptor-launcher-imgui STATIC
    "${disruptor_imgui_SOURCE_DIR}/imgui.cpp"
    "${disruptor_imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${disruptor_imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${disruptor_imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${disruptor_imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp"
    "${disruptor_imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${disruptor_imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(disruptor-launcher-imgui PUBLIC
    "${disruptor_imgui_SOURCE_DIR}"
    "${disruptor_imgui_SOURCE_DIR}/backends")
target_compile_definitions(disruptor-launcher-imgui PUBLIC IMGUI_ENABLE_FREETYPE)
target_link_libraries(disruptor-launcher-imgui PUBLIC ${DISRUPTOR_LAUNCHER_SDL_LIBRARY} freetype)
target_compile_features(disruptor-launcher-imgui PUBLIC cxx_std_20)

add_executable(disruptor-launcher WIN32
    main.cpp session.cpp game_process.cpp view.cpp activity.cpp update_flow.cpp state_upgrade.cpp art.cpp)
set_target_properties(disruptor-launcher PROPERTIES
    OUTPUT_NAME DisruptorLauncher
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_link_libraries(disruptor-launcher PRIVATE
    disruptor-launcher-model disruptor-launcher-imgui ${DISRUPTOR_LAUNCHER_SDL_LIBRARY} $<$<BOOL:${WIN32}>:shell32 dwmapi>)
target_include_directories(disruptor-launcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_property(SOURCE view.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    "DISRUPTOR_BUILD_VERSION=\"${DISRUPTOR_BUILD_VERSION}\"")
file(GLOB DISRUPTOR_LAUNCHER_ASSETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
set(DISRUPTOR_LAUNCHER_EMBEDDED
    "${DISRUPTOR_LAUNCHER_ASSETS};${PROJECT_SOURCE_DIR}/docs/PLAYING.md;${PROJECT_SOURCE_DIR}/CHANGELOG.md")
if(WIN32)
    target_sources(disruptor-launcher PRIVATE resources.rc)
    set_source_files_properties(resources.rc PROPERTIES OBJECT_DEPENDS "${DISRUPTOR_LAUNCHER_EMBEDDED}")
else()
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/resources.cpp"
        COMMAND "${CMAKE_COMMAND}" "-DRC_FILE=${CMAKE_CURRENT_SOURCE_DIR}/resources.rc"
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/resources.cpp" -P "${PROJECT_SOURCE_DIR}/cmake/embed_resources.cmake"
        DEPENDS resources.rc "${PROJECT_SOURCE_DIR}/cmake/embed_resources.cmake" ${DISRUPTOR_LAUNCHER_EMBEDDED}
        VERBATIM)
    target_sources(disruptor-launcher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/resources.cpp")
endif()
if(DISRUPTOR_VELOPACK_LIBRARY)
    target_sources(disruptor-launcher PRIVATE updater.cpp)
    target_link_libraries(disruptor-launcher PRIVATE disruptor-velopack)
    target_compile_definitions(disruptor-launcher PRIVATE DISRUPTOR_UPDATER=1)
else()
    target_sources(disruptor-launcher PRIVATE updater_none.cpp)
endif()
if(MINGW)
    target_sources(disruptor-launcher PRIVATE launcher.rc)
    # Under UNICODE SDL_main.h defines wWinMain, which MinGW's start code calls only with -municode.
    target_link_options(disruptor-launcher PRIVATE -static -municode)
endif()
if(MSVC)
    target_sources(disruptor-launcher PRIVATE launcher.manifest)
    # Setup itself must open even before the VC++ redistributable is installed.
    set_property(TARGET disruptor-disc-import disruptor-launcher-config
                        disruptor-launcher-model disruptor-launcher-imgui disruptor-launcher freetype
        PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(disruptor-disc-import PRIVATE /W4 /utf-8)
    target_compile_options(disruptor-launcher-config PRIVATE /utf-8)
    target_compile_options(disruptor-launcher-model PRIVATE /W4 /utf-8)
    target_compile_options(disruptor-launcher-imgui PRIVATE /utf-8)
    target_compile_options(disruptor-launcher PRIVATE /W4 /utf-8)
endif()

# A normal local Windows build is launchable as well as the packaged release.
# Preserve local bindings and preferences if they already exist.
add_custom_command(TARGET disruptor-launcher POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE=${PROJECT_SOURCE_DIR}"
        "-DDESTINATION=$<TARGET_FILE_DIR:disruptor-launcher>"
        -P "${PROJECT_SOURCE_DIR}/cmake/stage_launcher.cmake"
    VERBATIM)
set_property(TARGET disruptor-launcher APPEND PROPERTY LINK_DEPENDS
    "${PROJECT_SOURCE_DIR}/cmake/stage_launcher.cmake"
    "${PROJECT_SOURCE_DIR}/game.toml"
    "${PROJECT_SOURCE_DIR}/mouse-aim.ini"
    "${PROJECT_SOURCE_DIR}/docs/PLAYING.md"
    "${PROJECT_SOURCE_DIR}/config/keybinds-modern.ini")

if(BUILD_TESTING)
    add_executable(disruptor-disc-import-test "${PROJECT_SOURCE_DIR}/tests/unit/disc_import_test.cpp")
    set_target_properties(disruptor-disc-import-test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_link_libraries(disruptor-disc-import-test PRIVATE disruptor-disc-import)
    if(MSVC)
        set_property(TARGET disruptor-disc-import-test
            PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(disruptor-disc-import-test PRIVATE /utf-8)
    endif()
    add_test(NAME disruptor_disc_import COMMAND disruptor-disc-import-test)

    add_executable(disruptor-launcher-settings-test "${PROJECT_SOURCE_DIR}/tests/unit/launcher_settings_test.cpp")
    target_link_libraries(disruptor-launcher-settings-test PRIVATE disruptor-launcher-model)
    target_compile_definitions(disruptor-launcher-settings-test PRIVATE "DISRUPTOR_SOURCE_DIR=LR\"(${PROJECT_SOURCE_DIR})\"")
    add_test(NAME disruptor_launcher_settings COMMAND disruptor-launcher-settings-test)

    add_executable(disruptor-launcher-flow-test "${PROJECT_SOURCE_DIR}/tests/unit/launcher_flow_test.cpp")
    target_link_libraries(disruptor-launcher-flow-test PRIVATE disruptor-launcher-model)
    add_test(NAME disruptor_launcher_flow COMMAND disruptor-launcher-flow-test)

    add_executable(disruptor-launcher-guide-test "${PROJECT_SOURCE_DIR}/tests/unit/launcher_guide_test.cpp")
    target_link_libraries(disruptor-launcher-guide-test PRIVATE disruptor-launcher-model)
    target_compile_definitions(disruptor-launcher-guide-test PRIVATE "DISRUPTOR_SOURCE_DIR=R\"(${PROJECT_SOURCE_DIR})\"")
    add_test(NAME disruptor_launcher_guide COMMAND disruptor-launcher-guide-test)

    add_executable(disruptor-launcher-release-notes-test "${PROJECT_SOURCE_DIR}/tests/unit/launcher_release_notes_test.cpp")
    target_link_libraries(disruptor-launcher-release-notes-test PRIVATE disruptor-launcher-model)
    target_compile_definitions(disruptor-launcher-release-notes-test PRIVATE "DISRUPTOR_SOURCE_DIR=R\"(${PROJECT_SOURCE_DIR})\"")
    add_test(NAME disruptor_launcher_release_notes COMMAND disruptor-launcher-release-notes-test)

    add_executable(disruptor-launcher-key-bindings-test "${PROJECT_SOURCE_DIR}/tests/unit/launcher_key_bindings_test.cpp")
    target_link_libraries(disruptor-launcher-key-bindings-test PRIVATE disruptor-launcher-model)
    add_test(NAME disruptor_launcher_key_bindings COMMAND disruptor-launcher-key-bindings-test)

    add_executable(disruptor-launcher-pad-bindings-test "${PROJECT_SOURCE_DIR}/tests/unit/launcher_pad_bindings_test.cpp")
    target_link_libraries(disruptor-launcher-pad-bindings-test PRIVATE disruptor-launcher-model)
    add_test(NAME disruptor_launcher_pad_bindings COMMAND disruptor-launcher-pad-bindings-test)

    add_executable(disruptor-launcher-platform-test "${PROJECT_SOURCE_DIR}/tests/unit/launcher_platform_test.cpp")
    target_link_libraries(disruptor-launcher-platform-test PRIVATE disruptor-launcher-model)
    add_test(NAME disruptor_launcher_platform COMMAND disruptor-launcher-platform-test)

    add_executable(disruptor-launcher-report-test "${PROJECT_SOURCE_DIR}/tests/unit/launcher_report_test.cpp")
    target_link_libraries(disruptor-launcher-report-test PRIVATE disruptor-launcher-model)
    add_test(NAME disruptor_launcher_report COMMAND disruptor-launcher-report-test)

    add_executable(disruptor-launcher-player-files-test "${PROJECT_SOURCE_DIR}/tests/unit/launcher_player_files_test.cpp")
    target_link_libraries(disruptor-launcher-player-files-test PRIVATE disruptor-launcher-model)
    add_test(NAME disruptor_launcher_player_files COMMAND disruptor-launcher-player-files-test)

    add_executable(disruptor-launcher-state-stamp-test "${PROJECT_SOURCE_DIR}/tests/unit/launcher_state_stamp_test.cpp")
    target_link_libraries(disruptor-launcher-state-stamp-test PRIVATE disruptor-launcher-model)
    add_test(NAME disruptor_launcher_state_stamp COMMAND disruptor-launcher-state-stamp-test)

    add_executable(disruptor-achievements-test "${PROJECT_SOURCE_DIR}/tests/unit/achievements_test.cpp")
    target_link_libraries(disruptor-achievements-test PRIVATE disruptor-launcher-model)
    target_include_directories(disruptor-achievements-test PRIVATE "${disruptor_rcheevos_SOURCE_DIR}/src")
    add_test(NAME disruptor_achievements COMMAND disruptor-achievements-test)

    # The synthetic discs include the layout code whole, so this test takes region_disc.cpp and not the library.
    add_executable(disruptor-region-disc-test
        "${PROJECT_SOURCE_DIR}/tests/unit/region_disc_test.cpp" region_disc.cpp)
    target_include_directories(disruptor-region-disc-test PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(disruptor-region-disc-test PRIVATE disruptor-disc-import)
    add_test(NAME disruptor_region_disc COMMAND disruptor-region-disc-test)

    set_target_properties(disruptor-launcher-settings-test disruptor-launcher-flow-test disruptor-launcher-guide-test
        disruptor-launcher-release-notes-test
        disruptor-launcher-key-bindings-test disruptor-launcher-pad-bindings-test disruptor-launcher-report-test
        disruptor-launcher-platform-test
        disruptor-launcher-player-files-test disruptor-launcher-state-stamp-test disruptor-region-disc-test
        disruptor-achievements-test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    if(MSVC)
        set_property(TARGET disruptor-launcher-settings-test disruptor-launcher-flow-test disruptor-launcher-guide-test
                            disruptor-launcher-release-notes-test
                            disruptor-launcher-key-bindings-test disruptor-launcher-pad-bindings-test disruptor-launcher-report-test
                            disruptor-launcher-platform-test
                            disruptor-launcher-player-files-test disruptor-launcher-state-stamp-test disruptor-region-disc-test
                            disruptor-achievements-test
            PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(disruptor-launcher-settings-test PRIVATE /utf-8)
        target_compile_options(disruptor-launcher-flow-test PRIVATE /utf-8)
        target_compile_options(disruptor-launcher-guide-test PRIVATE /W4 /utf-8)
        target_compile_options(disruptor-launcher-release-notes-test PRIVATE /W4 /utf-8)
        target_compile_options(disruptor-launcher-key-bindings-test PRIVATE /W4 /utf-8)
        target_compile_options(disruptor-launcher-pad-bindings-test PRIVATE /W4 /utf-8)
        target_compile_options(disruptor-launcher-report-test PRIVATE /utf-8)
        target_compile_options(disruptor-launcher-platform-test PRIVATE /W4 /utf-8)
        target_compile_options(disruptor-launcher-player-files-test PRIVATE /W4 /utf-8)
        target_compile_options(disruptor-launcher-state-stamp-test PRIVATE /W4 /utf-8)
        target_compile_options(disruptor-region-disc-test PRIVATE /utf-8)
        target_compile_options(disruptor-achievements-test PRIVATE /W4 /utf-8)
    endif()
endif()
