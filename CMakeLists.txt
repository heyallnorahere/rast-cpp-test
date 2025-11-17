cmake_minimum_required(VERSION 3.20)
project(rast-cpp-test LANGUAGES C CXX)

add_subdirectory("vendor")

file(GLOB_RECURSE SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(rast-cpp-test ${SRC})
target_link_libraries(rast-cpp-test PRIVATE glm rast)
set_target_properties(rast-cpp-test PROPERTIES CXX_STANDARD 20)
