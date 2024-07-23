find_package(Boost COMPONENTS unit_test_framework)
add_executable(RezUnitTests UnitTests.cc)
target_link_libraries(RezUnitTests PRIVATE
	RezLib
	Boost::headers
	Boost::unit_test_framework)
add_test(NAME RezUnitTests COMMAND RezUnitTests)
