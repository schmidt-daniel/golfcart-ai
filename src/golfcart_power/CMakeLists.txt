cmake_minimum_required(VERSION 3.16)
project(golfcart_power)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(golfcart_msgs REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(auto_shutdown_node src/auto_shutdown_node.cpp)
target_link_libraries(auto_shutdown_node rclcpp::rclcpp golfcart_msgs::golfcart_msgs std_msgs::std_msgs)

install(TARGETS auto_shutdown_node
  DESTINATION lib/${PROJECT_NAME})

ament_package()