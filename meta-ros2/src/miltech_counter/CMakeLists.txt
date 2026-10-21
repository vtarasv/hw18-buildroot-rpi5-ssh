cmake_minimum_required(VERSION 3.8)
project(miltech_counter)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)

foreach(node counter_pub counter_sub counter_switch)
  add_executable(${node} src/${node}.cpp)
  target_link_libraries(${node} rclcpp::rclcpp ${std_msgs_TARGETS} ${std_srvs_TARGETS})
  install(TARGETS ${node} DESTINATION lib/${PROJECT_NAME})
endforeach()

ament_package()
