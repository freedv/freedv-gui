#
# freedv_embed_files(<header> <prefix> <file>...)
#
# Writes a C++ header holding the bytes of each file, as
#
#   static const unsigned char <prefix>_<n>[] = { ... };
#
# (n counting from 0, in the order given) and an array of them:
#
#   static const struct { const unsigned char* data; unsigned long size; } <prefix>[] = { ... };
#
# It's written when CMake runs, which it does again whenever one of the files
# changes.
#
function(freedv_embed_files header prefix)
    set(contents "// Made by freedv_embed_files() (cmake/EmbedFiles.cmake). Don't edit.\n#pragma once\n\n")
    set(table "")
    set(index 0)
    foreach(file ${ARGN})
        file(READ ${file} hex HEX)
        string(LENGTH "${hex}" hex_length)
        math(EXPR size "${hex_length} / 2")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
        # (A line break now and then, for whatever opens the header.)
        string(REGEX REPLACE "((0x..,){32})" "\\1\n" bytes "${bytes}")
        string(APPEND contents "// ${file}\nstatic const unsigned char ${prefix}_${index}[] = {\n${bytes}\n};\n\n")
        string(APPEND table "    { ${prefix}_${index}, ${size} },\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${file})
        math(EXPR index "${index} + 1")
    endforeach(file)
    string(APPEND contents "static const struct { const unsigned char* data; unsigned long size; } ${prefix}[] = {\n${table}};\n")

    # Only written when it changes, so what includes it isn't rebuilt every time.
    if(EXISTS ${header})
        file(READ ${header} old)
    endif()
    if(NOT "${old}" STREQUAL "${contents}")
        file(WRITE ${header} "${contents}")
    endif()
endfunction()
