cmake_minimum_required(VERSION 3.16)
project(golfcart_gps)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(golfcart_msgs REQUIRED)

add_library(golfcart_gps
  src/mock_gps_sensor.cpp
  src/gps_sensor_impl.cpp
)
target_include_directories(golfcart_gps PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(golfcart_gps PUBLIC rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

add_executable(gps_node src/gps_node.cpp)
target_link_libraries(gps_node golfcart_gps rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

install(TARGETS gps_node
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_mock_gps test/test_mock_gps.cpp)
  if(TARGET test_mock_gps)
    target_link_libraries(test_mock_gps golfcart_gps)
  endif()
endif()

ament_export_dependencies(rclcpp golfcart_msgs)
ament_export_include_directories(include)
ament_export_libraries(golfcart_gps)

ament_package()