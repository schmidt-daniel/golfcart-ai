cmake_minimum_required(VERSION 3.16)
project(golfcart_localization)



if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(golfcart_msgs REQUIRED)
find_package(golfcart_control REQUIRED)



add_executable(wheel_odometry_node src/wheel_odometry_node.cpp)
target_include_directories(wheel_odometry_node PRIVATE
  ${golfcart_control_INCLUDE_DIRS}
  ${tf2_INCLUDE_DIRS}
)
target_link_libraries(wheel_odometry_node rclcpp::rclcpp nav_msgs::nav_msgs tf2::tf2 tf2_ros::tf2_ros geometry_msgs::geometry_msgs golfcart_msgs::golfcart_msgs)

add_executable(sensor_fusion_node src/sensor_fusion_node.cpp)
target_include_directories(sensor_fusion_node PRIVATE
  ${tf2_INCLUDE_DIRS}
)
target_link_libraries(sensor_fusion_node rclcpp::rclcpp sensor_msgs::sensor_msgs tf2::tf2 golfcart_msgs::golfcart_msgs)

add_executable(localization_quality_node src/localization_quality_node.cpp)
target_link_libraries(localization_quality_node rclcpp::rclcpp nav_msgs::nav_msgs golfcart_msgs::golfcart_msgs)

install(TARGETS wheel_odometry_node sensor_fusion_node localization_quality_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_wheel_odometry test/test_wheel_odometry.cpp)
  if(TARGET test_wheel_odometry)
    target_include_directories(test_wheel_odometry PRIVATE
      ${golfcart_control_INCLUDE_DIRS}
    )
  endif()
endif()

ament_export_dependencies(rclcpp golfcart_msgs golfcart_control)

ament_package()