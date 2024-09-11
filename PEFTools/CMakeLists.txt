add_executable(MakePEF MakePEF.cc rs6000.h PEF.h)

add_executable(MakeImport MakeImport.cc PEF.h)
target_link_libraries(MakeImport PRIVATE ResourceFiles)

install(TARGETS MakePEF MakeImport)
