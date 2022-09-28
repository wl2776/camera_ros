cmake_minimum_required(VERSION 3.5)
project(camera_test)

set(CMAKE_CXX_STANDARD 17)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
  add_link_options("-Wl,-z,relro,-z,now,-z,defs")
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(libcamera REQUIRED libcamera)

find_package(OpenCV REQUIRED)

add_executable(camera_test CameraNode.cpp)

target_include_directories(camera_test PUBLIC ${libcamera_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(camera_test ${libcamera_LINK_LIBRARIES} ${OpenCV_LIBS})

install(TARGETS camera_test DESTINATION bin)
