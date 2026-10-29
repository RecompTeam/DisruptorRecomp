# Writes OUTPUT defining DISRUPTOR_BUILD_VERSION: v and RELEASE when given, else what git describe says of SOURCE_DIR.
if(RELEASE)
    set(version "v${RELEASE}")
else()
    execute_process(COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY "${SOURCE_DIR}" OUTPUT_VARIABLE version
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE failed)
    if(failed OR NOT version)
        set(version "unknown")
    endif()
endif()
file(WRITE "${OUTPUT}.new" "#pragma once\n\n#define DISRUPTOR_BUILD_VERSION \"${version}\"\n")
file(COPY_FILE "${OUTPUT}.new" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.new")
