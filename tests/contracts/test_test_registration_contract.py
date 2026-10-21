#!/usr/bin/env python3
"""Source contract: every contract and every unit test in the tree is registered once, through the two helper functions.

A contract or a test file that nothing registers is never run, and nothing else would say so.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ARTIFACTS = "--require-" + "artifacts"
EXPLICIT = {
    "test_borderless_contract.py": "host_borderless_contract",
    "test_framework_tool.py": "disruptor_framework_tool",
}
CONTRACT_HELPER = """function(disruptor_contract name)
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
"""
UNIT_HELPER = """function(disruptor_unit_test test source)
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
    add_test(NAME ${test} COMMAND ${target} ${UNIT_ARGUMENTS})
endfunction()
"""


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def contract_faults(cmake: str, scripts: dict[str, bool]) -> list[str]:
    """What is wrong with how the build file registers these contract files, each given with whether it takes artifacts."""
    faults = []
    for script, retail in sorted(scripts.items()):
        if script in EXPLICIT:
            names = cmake.count(f"    add_test(NAME {EXPLICIT[script]}\n")
            runs = cmake.count(f"/tests/contracts/{script}")
            if names != 1 or runs != 1:
                faults.append(f"{script} is named {names} times and run {runs} times, not once each")
            continue
        name = re.fullmatch(r"test_(\w+)_contract\.py", script)
        if name is None:
            faults.append(f"{script} is not named as a contract")
            continue
        plain = cmake.count(f"    disruptor_contract({name.group(1)})\n")
        marked = cmake.count(f"    disruptor_contract({name.group(1)} RETAIL)\n")
        if (plain, marked) != ((0, 1) if retail else (1, 0)):
            faults.append(f"{script} is registered {plain} times plainly and {marked} times as retail")
        if f"/tests/contracts/{script}" in cmake:
            faults.append(f"{script} is also run by name")
    registered = re.findall(r"^\s*disruptor_contract\((\w+)", cmake, re.MULTILINE)
    faults += [
        f"{name} is registered and has no file" for name in registered if f"test_{name}_contract.py" not in scripts
    ]
    return faults


def unit_faults(builds: str, cmake: str, sources: list[str]) -> list[str]:
    """What is wrong with how the build files register these unit test files."""
    faults = [f"{source} is built {builds.count(source)} times" for source in sources if builds.count(source) != 1]
    tail = cmake.split("include(CTest)")[1]
    units = re.findall(r"^\s*disruptor_unit_test\((\w+) (tests/unit/\w+\.cpp)", tail, re.MULTILINE)
    tests = [test for test, _ in units]
    faults += [f"{test} is registered twice" for test in sorted(set(tests)) if tests.count(test) > 1]
    faults += [f"{source} has no file" for _, source in units if source not in sources]
    if "add_executable(" in tail:
        faults.append("an executable is built past the helper")
    return faults


cmake = read("CMakeLists.txt")
launcher_cmake = read("src/launcher/CMakeLists.txt")
helpers = read("cmake/DisruptorTests.cmake")

require(
    '    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DisruptorTests.cmake")\n' in cmake
    and cmake.index("find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)")
    < cmake.index("cmake/DisruptorTests.cmake")
    and cmake.index("cmake/DisruptorTests.cmake") < cmake.index("disruptor_contract("),
    "the helpers are loaded after Python is found and before the first registration",
)
require(
    helpers.count(CONTRACT_HELPER) == 1 and helpers.count("\nfunction(") == 2,
    "a contract runs its own file, and a retail one is told to require its artifacts and is labelled retail",
)
require(
    helpers.count(UNIT_HELPER) == 1,
    "a unit test is one executable named after its test, built with what it was given and always run under that name",
)

scripts = {
    path.name: ARTIFACTS in path.read_text(encoding="utf-8")
    for path in (ROOT / "tests/contracts").glob("test_*.py")
    if path.name != Path(__file__).name
}
scripts[Path(__file__).name] = False
require(contract_faults(cmake, scripts) == [], "; ".join(contract_faults(cmake, scripts)))
require(sum(scripts.values()) == 14, "fourteen contracts take the retail artifacts")

sources = sorted(f"tests/unit/{path.name}" for path in (ROOT / "tests/unit").glob("*.cpp"))
require(
    unit_faults(cmake + launcher_cmake, cmake, sources) == [],
    "; ".join(unit_faults(cmake + launcher_cmake, cmake, sources)),
)

plain_line, retail_line = "    disruptor_contract(depth24_sync)\n", "    disruptor_contract(name_entry RETAIL)\n"
again = '    add_test(NAME host_borderless_again\n        COMMAND x "${CMAKE_CURRENT_SOURCE_DIR}/tests/contracts/test_borderless_contract.py")\n'
unit_line = "    disruptor_unit_test(host_pick_slot tests/unit/host_pick_slot_test.cpp)\n"
require(
    cmake.count(plain_line) == cmake.count(retail_line) == cmake.count(unit_line) == 1
    and contract_faults(cmake.replace(plain_line, ""), scripts) != []
    and contract_faults(cmake.replace(plain_line, plain_line * 2), scripts) != []
    and contract_faults(cmake.replace(plain_line, "    disruptor_contract(depth24_sync RETAIL)\n"), scripts) != []
    and contract_faults(cmake.replace(retail_line, "    disruptor_contract(name_entry)\n"), scripts) != []
    and contract_faults(cmake.replace(plain_line, plain_line + "    disruptor_contract(no_such)\n"), scripts) != []
    and contract_faults(cmake.replace(plain_line, plain_line + again), scripts) != []
    and contract_faults(cmake, scripts | {"test_new_contract.py": False}) != []
    and unit_faults((cmake + launcher_cmake).replace(unit_line, ""), cmake.replace(unit_line, ""), sources) != []
    and unit_faults(cmake + launcher_cmake + unit_line, cmake + unit_line, sources) != []
    and unit_faults(cmake + launcher_cmake, cmake, [*sources, "tests/unit/new_test.cpp"]) != []
    and unit_faults(cmake + launcher_cmake, cmake + "    add_executable(extra x.cpp)\n", sources) != [],
    "a missing, doubled, mislabelled or fileless registration is seen, in the contracts and in the unit tests",
)

print("Disruptor test registration source contract: PASS")
