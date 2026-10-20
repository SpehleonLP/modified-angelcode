find_package(GTest REQUIRED)

add_executable(as_tests test_as_halting.cpp test_as_halting_obvious.cpp test_as_app_context.cpp test_as_compile_method.cpp test_as_memory_access_handles.cpp test_as_pure_fold.cpp test_as_parse_condition.cpp test_as_literal_method_fold.cpp test_as_initializer_constructor.cpp test_as_script_object_lifecycle.cpp test_as_dual_refcount.cpp test_as_loops_fact.cpp
	${CMAKE_SOURCE_DIR}/sdk/add_on/scriptstdstring/scriptstdstring.cpp)
target_link_libraries(as_tests PRIVATE angelscript GTest::gtest GTest::gtest_main Threads::Threads)
target_include_directories(as_tests PRIVATE
	${CMAKE_SOURCE_DIR}/sdk/angelscript/include
	${CMAKE_SOURCE_DIR}/sdk/angelscript/source
	${CMAKE_SOURCE_DIR}/sdk/add_on)

include(GoogleTest)
gtest_discover_tests(as_tests)
