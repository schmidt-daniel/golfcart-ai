cmake_minimum_required(VERSION 3.16)
project(golfcart_behavior)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(golfcart_msgs REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(hill_rollback_node src/hill_rollback_node.cpp)
target_link_libraries(hill_rollback_node rclcpp::rclcpp golfcart_msgs::golfcart_msgs std_msgs::std_msgs)

add_executable(push_assist_node src/push_assist_node.cpp)
target_link_libraries(push_assist_node rclcpp::rclcpp golfcart_msgs::golfcart_msgs std_msgs::std_msgs)

install(TARGETS hill_rollback_node push_assist_node
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_hill_rollback test/test_hill_rollback.cpp)
  if(TARGET test_hill_rollback)
    target_link_libraries(test_hill_rollback rclcpp::rclcpp golfcart_msgs::golfcart_msgs)
  endif()
  ament_add_gtest(test_push_assist test/test_push_assist.cpp)
  if(TARGET test_push_assist)
    target_link_libraries(test_push_assist rclcpp::rclcpp golfcart_msgs::golfcart_msgs)
  endif()
endif()

ament_package()