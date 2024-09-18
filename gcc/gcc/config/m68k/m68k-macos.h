#define RETRO68_EXTRALIB "-lInterface"
#define RETRO68_EXTRALINK "-undefined=_consolewrite"

#include "m68k-maccommon.h"

#define POINTERS_IN_D0 1

#undef STARTFILE_SPEC
#define STARTFILE_SPEC ""

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()          \
  do                                      \
    {                                     \
      builtin_define ("macintosh");       \
      builtin_define ("Macintosh");       \
    }                                     \
  while (0)
