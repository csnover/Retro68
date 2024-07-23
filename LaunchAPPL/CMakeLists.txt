add_subdirectory(Common)

if(CMAKE_CROSSCOMPILING)
    add_subdirectory(Server)
else()
    add_subdirectory(Client)
endif()
