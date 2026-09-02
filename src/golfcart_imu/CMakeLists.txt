cmake_minimum_required(VERSION 3.16)
project(golfcart_imu)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(golfcart_msgs REQUIRED)

add_library(golfcart_imu
  src/mock_imu_sensor.cpp
  src/imu_sensor_impl.cpp
)
target_include_directories(golfcart_imu PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(golfcart_imu PUBLIC rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

add_executable(imu_node src/imu_node.cpp)
target_link_libraries(imu_node golfcart_imu rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

install(TARGETS imu_node
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_mock_imu test/test_mock_imu.cpp)
  if(TARGET test_mock_imu)
    target_link_libraries(test_mock_imu golfcart_imu)
  endif()
endif()

ament_export_dependencies(rclcpp golfcart_msgs)
ament_export_include_directories(include)
ament_export_libraries(golfcart_imu)

ament_package()