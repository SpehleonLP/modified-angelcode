find_package(GTest REQUIRED)

add_executable(as_tests test_as_halting.cpp)
target_link_libraries(as_tests PRIVATE angelscript GTest::gtest GTest::gtest_main Threads::Threads)
target_include_directories(as_tests PRIVATE ${CMAKE_SOURCE_DIR}/sdk/angelscript/include)

include(GoogleTest)
gtest_discover_tests(as_tests)
