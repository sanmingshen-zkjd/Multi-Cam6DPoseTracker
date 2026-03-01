cmake_minimum_required(VERSION 3.16)
project(multicam_pose_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(multicam_pose_backend
  src/server_main.cpp
)

if (WIN32)
  target_link_libraries(multicam_pose_backend PRIVATE ws2_32)
endif()
