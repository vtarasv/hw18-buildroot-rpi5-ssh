// Prints every number received on /counter.
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int32.hpp>

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = rclcpp::Node::make_shared("counter_sub");
  auto log = node->get_logger();
  auto sub = node->create_subscription<std_msgs::msg::UInt32>("counter", 10,
    [log](const std_msgs::msg::UInt32 & msg) { RCLCPP_INFO(log, "got %u", msg.data); });
  rclcpp::spin(node);
  rclcpp::shutdown();
}
