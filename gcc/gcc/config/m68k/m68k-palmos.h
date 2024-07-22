// Derived from m68k-macos.h from Retro68, and m68kpalmos.h from prc-tools-2.3

#define LIBGCC_SPEC "--start-group -lretrocrt -lgcc --end-group "
#define LINK_SPEC "-elf2mac -q"

#undef  LIB_SPEC
#define LIB_SPEC "--start-group -lc -lretrocrt --end-group"

#define LINK_GCC_C_SEQUENCE_SPEC "--start-group -lgcc -lc -lretrocrt --end-group"

#undef STARTFILE_SPEC
// TODO: Shared library support, I guess.
// #define STARTFILE_SPEC "%{!shared:crt0.o%s} %{shared:scrt0.o%s}"
#define STARTFILE_SPEC "%{mbreak-on-start:gdbstub%O%s}"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC ""

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()          \
  do                                      \
    {                                     \
      builtin_define ("__palmos__");      \
      builtin_define ("palmos");          \
    }                                     \
  while (0)
