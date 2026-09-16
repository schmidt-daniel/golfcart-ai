cmake_minimum_required(VERSION 3.16)
project(golfcart_system)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(golfcart_msgs REQUIRED)

add_library(golfcart_system INTERFACE)
target_include_directories(golfcart_system INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(golfcart_system INTERFACE rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

add_executable(capability_node src/capability_node.cpp)
target_link_libraries(capability_node golfcart_system rclcpp::rclcpp sensor_msgs::sensor_msgs golfcart_msgs::golfcart_msgs)

add_executable(sensor_health_logger_node src/sensor_health_logger_node.cpp)
target_link_libraries(sensor_health_logger_node golfcart_system rclcpp::rclcpp golfcart_msgs::golfcart_msgs)

install(TARGETS capability_node sensor_health_logger_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_capability_math test/test_capability_math.cpp)
  if(TARGET test_capability_math)
    target_link_libraries(test_capability_math golfcart_system)
  endif()
endif()

ament_export_dependencies(rclcpp golfcart_msgs)
ament_export_include_directories(include)

ament_package()