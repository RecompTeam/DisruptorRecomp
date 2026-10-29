# RetroAchievements: the rcheevos client library and the project's own code around it. The archive has no CMake
# project, so its sources are named here.
FetchContent_Declare(disruptor_rcheevos
    URL
        "https://github.com/RetroAchievements/rcheevos/archive/refs/tags/v12.5.0.tar.gz"
    URL_HASH
        "SHA256=e6df83de4e18f0a19206e711e1e589624dcfcf6bf529c14f6dd2bb2d1ced983f"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(disruptor_rcheevos)

set(DISRUPTOR_RCHEEVOS_SOURCES)
foreach(source
        rapi/rc_api_common.c rapi/rc_api_info.c rapi/rc_api_runtime.c rapi/rc_api_user.c
        rc_client.c rc_compat.c rc_util.c rc_version.c
        rcheevos/alloc.c rcheevos/condition.c rcheevos/condset.c rcheevos/consoleinfo.c rcheevos/format.c
        rcheevos/lboard.c rcheevos/memref.c rcheevos/operand.c rcheevos/rc_validate.c rcheevos/richpresence.c
        rcheevos/runtime.c rcheevos/runtime_progress.c rcheevos/trigger.c rcheevos/value.c
        rhash/cdreader.c rhash/hash.c rhash/hash_disc.c rhash/md5.c)
    list(APPEND DISRUPTOR_RCHEEVOS_SOURCES "${disruptor_rcheevos_SOURCE_DIR}/src/${source}")
endforeach()

set(DISRUPTOR_ACHIEVEMENTS_SOURCES
    "${PROJECT_SOURCE_DIR}/src/game/achievements/account.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/achievements/badges.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/achievements/chime.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/achievements/guest_memory.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/achievements/login.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/achievements/policy.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/achievements/progress_file.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/achievements/requests.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/achievements/session.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/achievements/sound_clip.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/achievements/user_agent.cpp")
if(WIN32)
    list(APPEND DISRUPTOR_ACHIEVEMENTS_SOURCES "${PROJECT_SOURCE_DIR}/src/game/achievements/transport_windows.cpp")
else()
    list(APPEND DISRUPTOR_ACHIEVEMENTS_SOURCES "${PROJECT_SOURCE_DIR}/src/game/achievements/transport_curl.cpp")
endif()

# The version the server sees: a release's own, otherwise the newest release tag before this build.
if(DEFINED PSX_GAME_VERSION AND NOT PSX_GAME_VERSION STREQUAL "")
    set(DISRUPTOR_CLIENT_VERSION "${PSX_GAME_VERSION}")
else()
    execute_process(COMMAND git describe --tags --abbrev=0
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" OUTPUT_VARIABLE DISRUPTOR_CLIENT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE disruptor_client_version_failed)
    if(disruptor_client_version_failed OR NOT DISRUPTOR_CLIENT_VERSION)
        set(DISRUPTOR_CLIENT_VERSION "0.0.0")
    endif()
endif()

find_package(Threads REQUIRED)

# disruptor_achievements_library(<target> [STATIC_RUNTIME])
#
# The game links one copy and the launcher another, because the launcher is built on the static C runtime.
function(disruptor_achievements_library target)
    cmake_parse_arguments(PARSE_ARGV 1 LIBRARY "STATIC_RUNTIME" "" "")
    add_library(${target} STATIC ${DISRUPTOR_RCHEEVOS_SOURCES} ${DISRUPTOR_ACHIEVEMENTS_SOURCES})
    target_include_directories(${target} PUBLIC "${disruptor_rcheevos_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/src")
    target_include_directories(${target} PRIVATE "${PSXRECOMP_ROOT}/recompiler/lib/toml11" "${disruptor_rcheevos_SOURCE_DIR}/src")
    target_compile_definitions(${target}
        PUBLIC RC_CLIENT_SUPPORTS_HASH RC_HASH_NO_ROM RC_HASH_NO_ZIP RC_HASH_NO_ENCRYPTED
        PRIVATE "DISRUPTOR_CLIENT_VERSION=\"${DISRUPTOR_CLIENT_VERSION}\"")
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_link_libraries(${target} PUBLIC Threads::Threads)
    if(WIN32)
        target_compile_definitions(${target} PRIVATE NOMINMAX)
        target_link_libraries(${target} PUBLIC winhttp crypt32)
    else()
        target_link_libraries(${target} PUBLIC ${CMAKE_DL_LIBS})
    endif()
    if(MSVC)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4>" /utf-8)
        if(LIBRARY_STATIC_RUNTIME)
            set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    endif()
endfunction()
