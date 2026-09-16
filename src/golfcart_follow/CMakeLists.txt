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

add_library(golfcart_follow INTERFACE)
target_include_directories(golfcart_follow INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(golfcart_follow INTERFACE rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

add_executable(follow_controller_node src/follow_controller_node.cpp)
target_link_libraries(follow_controller_node golfcart_follow rclcpp::rclcpp std_srvs::std_srvs sensor_msgs::sensor_msgs golfcart_msgs::golfcart_msgs)

add_executable(obstacle_awareness_node src/obstacle_awareness_node.cpp)
target_link_libraries(obstacle_awareness_node rclcpp::rclcpp sensor_msgs::sensor_msgs golfcart_msgs::golfcart_msgs)

add_executable(steering_assist_node src/steering_assist_node.cpp)
target_link_libraries(steering_assist_node rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

add_executable(person_reid_node src/person_reid_node.cpp)
target_link_libraries(person_reid_node golfcart_follow rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

install(TARGETS follow_controller_node obstacle_awareness_node steering_assist_node person_reid_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY include/
  DESTINATION include
)

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_follow_controller test/test_follow_controller.cpp)
  if(TARGET test_follow_controller)
    target_link_libraries(test_follow_controller rclcpp::rclcpp golfcart_msgs::golfcart_msgs)
  endif()
  ament_add_gtest(test_steering_assist test/test_steering_assist.cpp)
  if(TARGET test_steering_assist)
    target_link_libraries(test_steering_assist rclcpp::rclcpp golfcart_msgs::golfcart_msgs)
  endif()
  ament_add_gtest(test_reid_math test/test_reid_math.cpp)
  if(TARGET test_reid_math)
    target_link_libraries(test_reid_math golfcart_follow)
  endif()
endif()

ament_package()