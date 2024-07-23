add_executable(ConvertObj ConvertObj.cc)
target_link_libraries(ConvertObj PRIVATE ResourceFiles)
install(TARGETS ConvertObj)
