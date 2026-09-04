cmake_minimum_required(VERSION 3.16)
project(golfcart_gazebo)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)

# Install the Python package.
ament_python_install_package(${PROJECT_NAME})

# Register the cmd_vel_converter as an executable.
install(PROGRAMS ${PROJECT_NAME}/cmd_vel_converter.py
  DESTINATION lib/${PROJECT_NAME}
)

# Install URDF/xacro, worlds, config, and launch files.
install(DIRECTORY urdf
  DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY worlds
  DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()