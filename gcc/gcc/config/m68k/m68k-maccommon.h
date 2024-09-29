#define LIBGCC_SPEC "--start-group -lretrocrt -lgcc " RETRO68_EXTRALIB " --end-group "
#define LINK_SPEC "-elf2mac -q " RETRO68_EXTRALINK

#undef  LIB_SPEC
#define LIB_SPEC "--start-group -lc -lretrocrt " RETRO68_EXTRALIB " --end-group"

#define LINK_GCC_C_SEQUENCE_SPEC "--start-group -lgcc -lc -lretrocrt " RETRO68_EXTRALIB " --end-group"

#undef CPP_SPEC
#define CPP_SPEC "%{!Wmultichar:-Wno-multichar} %{!Wtrigraphs:-Wno-trigraphs}"

/* Default to -fpic. */
#undef CC1_SPEC
#define CC1_SPEC "%{!mpcrel:%{!fno-pic:%{" NO_FPIE_AND_FPIC_SPEC ":-fpic}}}"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC ""

/* Make system function calls work even with -mnoshort ints.  */
#undef PARM_BOUNDARY
#define PARM_BOUNDARY 16
