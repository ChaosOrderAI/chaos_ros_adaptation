cmake_minimum_required(VERSION 3.8)
project(chaos_interfaces)
find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/RobotState.msg" "msg/CommandResult.msg" "msg/VelocityCommand.msg" "msg/ControlTrace.msg"
  "msg/DriverVelocityCommand.msg" "msg/SkillToken.msg"
  "msg/SkillIntent.msg" "msg/SkillEvent.msg" "msg/Announcement.msg"
  "srv/GetDescription.srv" "srv/ControlLease.srv" "srv/Stop.srv" "srv/SkillRequest.srv"
  "action/NavigatePose.action" "action/Flight.action" "action/SoftwareUpdate.action"
  "action/ExecuteSkill.action"
  DEPENDENCIES builtin_interfaces std_msgs geometry_msgs)
ament_export_dependencies(rosidl_default_runtime)
ament_package()
