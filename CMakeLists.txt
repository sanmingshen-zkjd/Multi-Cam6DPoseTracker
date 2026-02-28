cmake_minimum_required(VERSION 3.16)
project(multicam_rig_toolkit_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows target: Qt 5.15 + Visual Studio 2019 (MSVC)
# Use Qt5 Widgets only (no Qt6 fallback) per project requirement.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 5.15 COMPONENTS Widgets REQUIRED)

find_package(OpenCV REQUIRED)
find_package(Eigen3 REQUIRED)

add_executable(multicam_rig_toolkit_qt
  src/main.cpp
  src/MainWindow.h
  src/MainWindow.cpp
  src/Core.h
  src/Core.cpp
  src/CaptureWorker.h
  src/SolveWorker.h
)

target_link_libraries(multicam_rig_toolkit_qt
  PRIVATE
    Qt5::Widgets
    ${OpenCV_LIBS}
    Eigen3::Eigen
)
