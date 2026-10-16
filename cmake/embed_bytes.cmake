# Writes the bytes of INPUT as a C++ source at OUTPUT that defines NAME and NAME_size with C linkage.
file(READ "${INPUT}" bytes HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
file(WRITE "${OUTPUT}.new"
    "#include <cstddef>\n\nextern \"C\" const unsigned char ${NAME}[] = {${bytes}};\n"
    "extern \"C\" const std::size_t ${NAME}_size = sizeof(${NAME});\n")
file(COPY_FILE "${OUTPUT}.new" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.new")
