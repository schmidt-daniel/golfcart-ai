cmake_minimum_required(VERSION 3.16)
project(golfcart_msgs)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/MotionRequest.msg"
  "msg/MotorCommand.msg"
  "msg/MotorState.msg"
  "msg/BatteryState.msg"
  "msg/ImuData.msg"
  "msg/GpsFix.msg"
  "msg/Obstacle.msg"
  "msg/ObstacleState.msg"
  "msg/GoalPose.msg"
  "msg/NavigationStatus.msg"
  "msg/CourseFeature.msg"
  "msg/CourseMap.msg"
  "srv/SetGoal.srv"
  DEPENDENCIES builtin_interfaces nav_msgs geometry_msgs
)

ament_export_dependencies(rosidl_default_runtime)

ament_package()
