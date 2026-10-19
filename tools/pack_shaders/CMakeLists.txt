# frigate-pack-shaders is a build-time tool, so it is built for the machine running the build: the root build adds this
# directory directly for a native build and as a host ExternalProject when cross-compiling.
cmake_minimum_required(VERSION 3.16)
if(NOT CMAKE_PROJECT_NAME)
    project(frigate_pack_shaders CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()
find_package(ZLIB REQUIRED)

set(PACK_SHADERS_SRC ${CMAKE_CURRENT_LIST_DIR}/../../src)
add_library(frigate_pack_shaders_lib STATIC
    PackShaders.cpp
    ${PACK_SHADERS_SRC}/extract/O2rWriter.cpp
    ${PACK_SHADERS_SRC}/extract/Inflate.cpp)
target_include_directories(frigate_pack_shaders_lib PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${PACK_SHADERS_SRC})
target_link_libraries(frigate_pack_shaders_lib PUBLIC ZLIB::ZLIB)

add_executable(frigate-pack-shaders main.cpp)
target_link_libraries(frigate-pack-shaders PRIVATE frigate_pack_shaders_lib)
