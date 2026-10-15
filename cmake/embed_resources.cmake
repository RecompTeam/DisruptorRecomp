# Writes the RCDATA entries of RC_FILE as a C++ source at OUTPUT, for systems without Windows resources.
file(STRINGS "${RC_FILE}" entries REGEX "^[0-9]+ RCDATA \"[^\"]+\"$")
get_filename_component(base "${RC_FILE}" DIRECTORY)
set(arrays "")
set(cases "")
foreach(entry IN LISTS entries)
    string(REGEX MATCH "^([0-9]+) RCDATA \"([^\"]+)\"$" matched "${entry}")
    set(id "${CMAKE_MATCH_1}")
    file(READ "${base}/${CMAKE_MATCH_2}" bytes HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
    string(APPEND arrays "constexpr unsigned char resource_${id}[] = {${bytes}};\n")
    string(APPEND cases "    case ${id}: return std::as_bytes(std::span(resource_${id}));\n")
endforeach()
file(WRITE "${OUTPUT}.new"
    "#include \"art.h\"\n\n#include <span>\n\nnamespace disruptor::launcher {\nnamespace {\n${arrays}} // namespace\n\n"
    "std::span<const std::byte> resource_bytes(int id) {\n    switch (id) {\n${cases}    default: return {};\n    }\n}\n\n"
    "} // namespace disruptor::launcher\n")
file(COPY_FILE "${OUTPUT}.new" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.new")
