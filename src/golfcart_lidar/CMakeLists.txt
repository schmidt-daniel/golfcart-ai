cmake_minimum_required(VERSION 3.16)
project(golfcart_lidar)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(golfcart_msgs REQUIRED)

add_library(golfcart_lidar
  src/mock_lidar_sensor.cpp
  src/lidar_sensor_impl.cpp
)
target_include_directories(golfcart_lidar PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(golfcart_lidar PUBLIC rclcpp::rclcpp sensor_msgs::sensor_msgs golfcart_msgs::golfcart_msgs)

add_executable(lidar_node src/lidar_node.cpp)
target_link_libraries(lidar_node golfcart_lidar rclcpp::rclcpp sensor_msgs::sensor_msgs golfcart_msgs::golfcart_msgs)

add_executable(obstacle_detection_node src/obstacle_detection_node.cpp)
target_link_libraries(obstacle_detection_node rclcpp::rclcpp sensor_msgs::sensor_msgs golfcart_msgs::golfcart_msgs)

install(TARGETS lidar_node obstacle_detection_node
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_mock_lidar test/test_mock_lidar.cpp)
  if(TARGET test_mock_lidar)
    target_link_libraries(test_mock_lidar golfcart_lidar)
  endif()
endif()

ament_export_dependencies(rclcpp sensor_msgs golfcart_msgs)
ament_export_include_directories(include)
ament_export_libraries(golfcart_lidar)

ament_package()