cmake_minimum_required(VERSION 3.16)
project(golfcart_follow)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_srvs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(golfcart_msgs REQUIRED)

add_executable(follow_controller_node src/follow_controller_node.cpp)
target_link_libraries(follow_controller_node rclcpp::rclcpp std_srvs::std_srvs sensor_msgs::sensor_msgs golfcart_msgs::golfcart_msgs)

add_executable(obstacle_awareness_node src/obstacle_awareness_node.cpp)
target_link_libraries(obstacle_awareness_node rclcpp::rclcpp sensor_msgs::sensor_msgs golfcart_msgs::golfcart_msgs)

install(TARGETS follow_controller_node obstacle_awareness_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_follow_controller test/test_follow_controller.cpp)
  if(TARGET test_follow_controller)
    target_link_libraries(test_follow_controller rclcpp::rclcpp golfcart_msgs::golfcart_msgs)
  endif()
endif()

ament_package()