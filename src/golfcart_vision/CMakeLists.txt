cmake_minimum_required(VERSION 3.16)
project(golfcart_vision)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(golfcart_msgs REQUIRED)

add_library(golfcart_vision INTERFACE)
target_include_directories(golfcart_vision INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(golfcart_vision INTERFACE rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

add_executable(gesture_recognition_node src/gesture_recognition_node.cpp)
target_link_libraries(gesture_recognition_node golfcart_vision rclcpp::rclcpp sensor_msgs::sensor_msgs golfcart_msgs::golfcart_msgs)

add_executable(gesture_controller_node src/gesture_controller_node.cpp)
target_link_libraries(gesture_controller_node golfcart_vision rclcpp::rclcpp std_srvs::std_srvs golfcart_msgs::golfcart_msgs)

install(TARGETS gesture_recognition_node gesture_controller_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY include/
  DESTINATION include
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_gesture_math test/test_gesture_math.cpp)
  if(TARGET test_gesture_math)
    target_link_libraries(test_gesture_math golfcart_vision)
  endif()
endif()

ament_export_dependencies(rclcpp golfcart_msgs)
ament_export_include_directories(include)

ament_package()