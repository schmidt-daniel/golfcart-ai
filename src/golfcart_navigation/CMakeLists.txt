cmake_minimum_required(VERSION 3.16)
project(golfcart_navigation)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(nav2_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(tf2 REQUIRED)
find_package(golfcart_msgs REQUIRED)

add_executable(navigation_node src/navigation_node.cpp)
target_include_directories(navigation_node PRIVATE
  ${tf2_INCLUDE_DIRS}
)
target_link_libraries(navigation_node rclcpp::rclcpp rclcpp_action::rclcpp_action nav_msgs::nav_msgs nav2_msgs::nav2_msgs geometry_msgs::geometry_msgs std_srvs::std_srvs tf2::tf2 golfcart_msgs::golfcart_msgs)

install(TARGETS navigation_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_navigation test/test_navigation.cpp)
  if(TARGET test_navigation)
    target_link_libraries(test_navigation golfcart_msgs::golfcart_msgs)
  endif()
endif()

ament_package()