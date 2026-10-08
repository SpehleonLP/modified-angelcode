cmake_minimum_required(VERSION 3.16)
project(modified-angelcode CXX)

enable_testing()

# AppContext.AppExceptionCaughtByEngineIsReportedToIt pins a type confusion
# that only UBSan's vptr check sees, so that check aborts; other findings
# print, as upstream has a few known ones.
option(AS_SANITIZE "Build with ASan and UBSan; vptr findings abort" OFF)
if(AS_SANITIZE)
	add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=vptr)
	add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(sdk/angelscript/projects/cmake angelscript)
add_subdirectory(tests)
