find_package(Boost COMPONENTS unit_test_framework QUIET)
if(NOT TARGET Boost::unit_test_framework)
	message(STATUS "Boost::unit_test_framework not found. Building Rez without tests")
	return()
endif()

add_executable(RezUnitTests UnitTests.cc)
target_link_libraries(RezUnitTests PRIVATE
	RezLib
	Boost::unit_test_framework)
add_test(NAME RezUnitTests COMMAND RezUnitTests)
