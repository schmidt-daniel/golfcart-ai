cmake_minimum_required(VERSION 3.16)
project(golfcart_control)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(std_srvs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(golfcart_msgs REQUIRED)

add_library(golfcart_control INTERFACE)
target_include_directories(golfcart_control INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(golfcart_control INTERFACE rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

add_executable(motion_controller_node src/motion_controller_node.cpp)
target_link_libraries(motion_controller_node golfcart_control rclcpp::rclcpp geometry_msgs::geometry_msgs golfcart_msgs::golfcart_msgs)

add_executable(safety_controller_node src/safety_controller_node.cpp)
target_link_libraries(safety_controller_node rclcpp::rclcpp std_srvs::std_srvs std_msgs::std_msgs geometry_msgs::geometry_msgs golfcart_msgs::golfcart_msgs)

install(TARGETS motion_controller_node safety_controller_node
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_differential_drive test/test_differential_drive.cpp)
  if(TARGET test_differential_drive)
    target_link_libraries(test_differential_drive golfcart_control)
  endif()
  ament_add_gtest(test_safety_state test/test_safety_state.cpp)
  if(TARGET test_safety_state)
    target_link_libraries(test_safety_state golfcart_control)
  endif()
endif()

ament_export_dependencies(rclcpp golfcart_msgs)
ament_export_include_directories(include)

ament_package()
