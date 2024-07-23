# To use this example as a standalone project using CMake:
# cmake -S <this directory> -B build \
#    -DCMAKE_TOOLCHAIN_FILE=path/to/Retro68/share/cmake/Retro.toolchain.cmake \
#    -DRETRO_ABI=m68k-apple-macos
# cmake --build build

add_application(HelloWorld hello.c CONSOLE)

# make the result as small as possible
# by removing unused code (gc-sections)
# and by removing macsbug function names on 68K
# (don't do this when debugging...)
set_target_properties(HelloWorld PROPERTIES COMPILE_OPTIONS -ffunction-sections)
if(RETRO_68K)
    set_target_properties(HelloWorld PROPERTIES
        LINK_FLAGS "-Wl,-gc-sections -Wl,--mac-strip-macsbug")
else()
    set_target_properties(HelloWorld PROPERTIES
        LINK_FLAGS "-Wl,-gc-sections")
endif()
