// Starts or stops counter_pub: counter_switch on|off
#include <chrono>
#include <cstdio>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>

using namespace std::chrono_literals;
using std_srvs::srv::SetBool;

int main(int argc, char ** argv)
{
  auto args = rclcpp::init_and_remove_ros_arguments(argc, argv);
  if (args.size() != 2 || (args[1] != "on" && args[1] != "off")) {
    fprintf(stderr, "usage: counter_switch on|off\n");
    return 2;
  }
  auto node = rclcpp::Node::make_shared("counter_switch");
  auto client = node->create_client<SetBool>("counter/enable");
  if (!client->wait_for_service(5s)) {
    RCLCPP_ERROR(node->get_logger(), "/counter/enable is not available");
    return 1;
  }
  auto req = std::make_shared<SetBool::Request>();
  req->data = args[1] == "on";
  auto future = client->async_send_request(req);
  if (rclcpp::spin_until_future_complete(node, future, 5s) != rclcpp::FutureReturnCode::SUCCESS) {
    RCLCPP_ERROR(node->get_logger(), "no response");
    return 1;
  }
  RCLCPP_INFO(node->get_logger(), "%s", future.get()->message.c_str());
  rclcpp::shutdown();
}
