// Derived from m68k-macos.h from Retro68, and m68kpalmos.h from prc-tools-2.3

#define RETRO68_EXTRALIB
#define RETRO68_EXTRALINK

#include "m68k-maccommon.h"

#undef STARTFILE_SPEC
// TODO: Shared library support, I guess.
// #define STARTFILE_SPEC "%{!shared:crt0.o%s} %{shared:scrt0.o%s}"
#define STARTFILE_SPEC "%{mbreak-on-start:gdbstub%O%s}"

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()          \
  do                                      \
    {                                     \
      builtin_define ("__palmos__");      \
      builtin_define ("palmos");          \
    }                                     \
  while (0)

#undef TARGET_DEFAULT_SHORT_ENUMS
#define TARGET_DEFAULT_SHORT_ENUMS hook_bool_void_true
