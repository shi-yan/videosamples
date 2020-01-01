cmake_minimum_required(VERSION 3.12)

project(libavmp4encoding LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(FFmpeg REQUIRED)

add_executable(libavmp4encoding
  main.cpp
)
target_link_libraries(libavmp4encoding ${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES})
