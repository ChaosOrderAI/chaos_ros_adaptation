#include "LIVMapper.h"

#include <exception>

int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<rclcpp::Node>("fast_livo2");
  int status = 0;
  try
  {
    LIVMapper mapper(node);
    mapper.initializeSubscribersAndPublishers();
    mapper.run();
  }
  catch (const std::exception &error)
  {
    // Bad parameters (type, extrinsic length, camera intrinsics) end up here.
    RCLCPP_FATAL(node->get_logger(), "%s", error.what());
    status = 1;
  }
  rclcpp::shutdown();
  return status;
}
