add_library(posergzip STATIC
    bits.c
    crypt.h
    deflate.c
    gzip.h
    inflate.c
    lzw.h
    revision.h
    tailor.h
    trees.c
    util.c
)

target_include_directories(posergzip PUBLIC .)
