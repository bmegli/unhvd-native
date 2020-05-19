cmake_minimum_required(VERSION 3.1)
set (CMAKE_CXX_STANDARD 11)

project(
    unhvd
)

# use fPIC for all libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(network-hardware-video-decoder)
add_subdirectory(hardware-depth-unprojector)

# this is our main target
add_library(unhvd SHARED unhvd.cpp)
target_include_directories(unhvd PRIVATE network-hardware-video-decoder)
target_include_directories(unhvd PRIVATE hardware-depth-unprojector)

# note that unhvd depends through nhvd on FFMpeg avcodec and avutil, at least 3.4 version
target_link_libraries(unhvd nhvd hdu)

add_executable(unhvd-frame-example examples/unhvd_frame_example.cpp)
target_link_libraries(unhvd-frame-example unhvd)

add_executable(unhvd-frame-multi-example examples/unhvd_frame_multi_example.cpp)
target_link_libraries(unhvd-frame-multi-example unhvd)

add_executable(unhvd-cloud-example examples/unhvd_cloud_example.cpp)
target_link_libraries(unhvd-cloud-example unhvd)

