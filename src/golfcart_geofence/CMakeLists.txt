cmake_minimum_required(VERSION 3.16)
project(golfcart_geofence)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_srvs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(golfcart_msgs REQUIRED)
find_package(yaml-cpp REQUIRED)

# geofence_node: /gps/fix -> /geofence/status (+ optional priority-3 stop motion)
add_executable(geofence_node src/geofence_node.cpp)
target_link_libraries(geofence_node rclcpp::rclcpp std_srvs::std_srvs sensor_msgs::sensor_msgs golfcart_msgs::golfcart_msgs yaml-cpp::yaml-cpp)

install(TARGETS geofence_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_geofence_math test/test_geofence_math.cpp)
  if(TARGET test_geofence_math)
    target_include_directories(test_geofence_math PRIVATE src)
  endif()
endif()

ament_package()