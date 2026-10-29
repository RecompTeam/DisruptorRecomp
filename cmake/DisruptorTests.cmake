# disruptor_contract(<name> [RETAIL])
#
# Registers tests/contracts/test_<name>_contract.py as the test disruptor_<name>_contract.
# RETAIL: the contract needs the retail image or the translated code. It is passed
# --require-artifacts and labelled so that `ctest -LE retail` leaves it out.
function(disruptor_contract name)
    cmake_parse_arguments(PARSE_ARGV 1 CONTRACT "RETAIL" "" "")
    if(CONTRACT_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "disruptor_contract(${name}): unknown arguments ${CONTRACT_UNPARSED_ARGUMENTS}")
    endif()
    set(script "${PROJECT_SOURCE_DIR}/tests/contracts/test_${name}_contract.py")
    if(NOT EXISTS "${script}")
        message(FATAL_ERROR "disruptor_contract(${name}): ${script} does not exist")
    endif()
    set(arguments "")
    if(CONTRACT_RETAIL)
        set(arguments --require-artifacts)
    endif()
    add_test(NAME disruptor_${name}_contract
        COMMAND "${Python3_EXECUTABLE}" -B "${script}" ${arguments})
    if(CONTRACT_RETAIL)
        set_tests_properties(disruptor_${name}_contract PROPERTIES LABELS retail)
    endif()
endfunction()

# disruptor_unit_test(<test> <source> [TARGET <target>] [RUNTIME_HEADERS]
#                     [SOURCES <file>...] [INCLUDES <folder>...] [DEFINITIONS <definition>...]
#                     [ARGUMENTS <argument>...])
#
# Builds one C++20 executable from <source>, a path from the project's root, and registers
# it as the test <test>. The executable is named after the test, disruptor_far_rendering
# giving disruptor-far-rendering-test, unless TARGET names it.
# RUNTIME_HEADERS puts the framework's runtime headers on its include path.
function(disruptor_unit_test test source)
    cmake_parse_arguments(PARSE_ARGV 2 UNIT "RUNTIME_HEADERS" "TARGET"
        "SOURCES;INCLUDES;DEFINITIONS;ARGUMENTS")
    if(UNIT_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "disruptor_unit_test(${test}): unknown arguments ${UNIT_UNPARSED_ARGUMENTS}")
    endif()
    set(target "${UNIT_TARGET}")
    if(NOT target)
        string(REPLACE "_" "-" target "${test}-test")
    endif()
    add_executable(${target} "${PROJECT_SOURCE_DIR}/${source}" ${UNIT_SOURCES})
    if(UNIT_RUNTIME_HEADERS)
        target_include_directories(${target} PRIVATE "${PSXRECOMP_ROOT}/runtime/include")
    endif()
    if(UNIT_INCLUDES)
        target_include_directories(${target} PRIVATE ${UNIT_INCLUDES})
    endif()
    if(UNIT_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${UNIT_DEFINITIONS})
    endif()
    target_compile_features(${target} PRIVATE cxx_std_20)
    # A C test is linked by the C driver, which leaves libm out on Linux.
    if(UNIX AND source MATCHES "\\.c$")
        target_link_libraries(${target} PRIVATE m)
    endif()
    add_test(NAME ${test} COMMAND ${target} ${UNIT_ARGUMENTS})
endfunction()
