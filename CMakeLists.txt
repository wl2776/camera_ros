cmake_minimum_required(VERSION 3.5)
project(camera_ros)

set(CMAKE_CXX_STANDARD 17)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
  add_link_options("-Wl,-z,relro,-z,now,-z,defs")
endif()

find_package(PkgConfig REQUIRED)

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(camera_info_manager REQUIRED)
find_package(cv_bridge REQUIRED)
pkg_check_modules(libcamera REQUIRED libcamera)

include(CheckTypeSize)
set(CMAKE_REQUIRED_INCLUDES ${libcamera_INCLUDE_DIRS})
set(CMAKE_EXTRA_INCLUDE_FILES "${libcamera_INCLUDE_DIRS}/libcamera/control_ids.h")

check_type_size("libcamera::controls::AfMode" AF_MODE LANGUAGE CXX)
check_type_size("libcamera::controls::AfRange" AF_RANGE LANGUAGE CXX)
check_type_size("libcamera::controls::AfSpeed" AF_SPEED LANGUAGE CXX)
check_type_size("libcamera::controls::AfMetering" AF_METERING LANGUAGE CXX)
check_type_size("libcamera::controls::AfWindows" AF_WINDOWS LANGUAGE CXX)
check_type_size("libcamera::controls::AfTrigger" AF_TRIGGER LANGUAGE CXX)
check_type_size("libcamera::controls::AfPause" AF_PAUSE LANGUAGE CXX)
check_type_size("libcamera::controls::LensPosition" LENS_POSITION LANGUAGE CXX)
check_type_size("libcamera::controls::AfState" AF_STATE LANGUAGE CXX)
check_type_size("libcamera::controls::AfPauseState" AF_PAUSE_STATE LANGUAGE CXX)
message(STATUS "Have AfMode ${HAVE_AF_MODE}")
message(STATUS "Have AfRange ${HAVE_AF_RANGE}")
message(STATUS "Have AfSpeed ${HAVE_AF_SPEED}")
message(STATUS "Have AfMetering ${HAVE_AF_METERING}")
message(STATUS "Have AfWindows ${HAVE_AF_WINDOWS}")
message(STATUS "Have AfTrigger ${HAVE_AF_TRIGGER}")
message(STATUS "Have AfPause ${HAVE_AF_PAUSE}")
message(STATUS "Have LensPosition ${HAVE_LENS_POSITION}")
message(STATUS "Have AfState ${HAVE_AF_STATE}")
message(STATUS "Have AfPauseState ${HAVE_AF_PAUSE_STATE}")

configure_file(src/config.hpp.in config.hpp)

# library with common utility functions for type conversions
add_library(utils OBJECT
  src/clamp.cpp
  src/cv_to_pv.cpp
  src/format_mapping.cpp
  src/parameter_conflict_check.cpp
  src/pretty_print.cpp
  src/pv_to_cv.cpp
  src/types.cpp
  src/type_extent.cpp
)
target_include_directories(utils PUBLIC ${libcamera_INCLUDE_DIRS})
target_include_directories(utils PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
ament_target_dependencies(
  utils
  "rclcpp"
  "sensor_msgs"
)
set_property(TARGET utils PROPERTY POSITION_INDEPENDENT_CODE ON)

# composable ROS2 node
add_library(camera_component SHARED src/CameraNode.cpp)
rclcpp_components_register_node(camera_component PLUGIN "camera::CameraNode" EXECUTABLE "camera_node")

target_include_directories(camera_component PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(
  camera_component
  "rclcpp"
  "rclcpp_components"
  "sensor_msgs"
  "camera_info_manager"
  "cv_bridge"
)

target_include_directories(camera_component PUBLIC ${libcamera_INCLUDE_DIRS})
target_link_libraries(camera_component ${libcamera_LINK_LIBRARIES} utils)

install(TARGETS camera_component
  DESTINATION lib)

if(BUILD_TESTING)
  set(ament_cmake_clang_format_CONFIG_FILE "${CMAKE_SOURCE_DIR}/.clang-format")
  find_package(ament_lint_auto REQUIRED)
  # the following line skips the linter which checks for copyrights
  # uncomment the line when a copyright and license is not present in all source files
  #set(ament_cmake_copyright_FOUND TRUE)
  # the following line skips cpplint (only works in a git repo)
  # uncomment the line when this package is not in a git repo
  #set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
