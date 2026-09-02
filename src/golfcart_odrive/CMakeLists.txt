cmake_minimum_required(VERSION 3.16)
project(golfcart_odrive)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(golfcart_msgs REQUIRED)

add_library(golfcart_odrive
  src/mock_motor_controller.cpp
  src/odrive_motor_controller.cpp
  src/odrive_protocol.cpp
  src/serial_port.cpp
)
target_include_directories(golfcart_odrive PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(golfcart_odrive PUBLIC rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

add_executable(odrive_node src/odrive_node.cpp)
target_link_libraries(odrive_node golfcart_odrive rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

install(TARGETS odrive_node
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_mock_motor_controller test/test_mock_motor_controller.cpp)
  if(TARGET test_mock_motor_controller)
    target_link_libraries(test_mock_motor_controller golfcart_odrive)
  endif()
  ament_add_gtest(test_odrive_protocol test/test_odrive_protocol.cpp)
  if(TARGET test_odrive_protocol)
    target_link_libraries(test_odrive_protocol golfcart_odrive)
  endif()
endif()

ament_export_dependencies(rclcpp golfcart_msgs)
ament_export_include_directories(include)
ament_export_libraries(golfcart_odrive)

ament_package()
