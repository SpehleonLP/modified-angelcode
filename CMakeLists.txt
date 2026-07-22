cmake_minimum_required(VERSION 3.16)
project(modified-angelcode CXX)

enable_testing()

add_subdirectory(sdk/angelscript/projects/cmake angelscript)
add_subdirectory(tests)
