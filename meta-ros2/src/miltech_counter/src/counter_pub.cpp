// Publishes 1, 2, 3... on /counter every second.
// Service /counter/enable (std_srvs/SetBool) starts and stops counting.
#include <chrono>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int32.hpp>
#include <std_srvs/srv/set_bool.hpp>

using namespace std::chrono_literals;
using std_msgs::msg::UInt32;
using std_srvs::srv::SetBool;

class CounterPub : public rclcpp::Node
{
public:
  CounterPub() : Node("counter_pub")
  {
    pub_ = create_publisher<UInt32>("counter", 10);
    timer_ = create_wall_timer(1s, [this] {
      if (!enabled_) return;
      UInt32 msg;
      msg.data = ++count_;
      RCLCPP_INFO(get_logger(), "publish %u", msg.data);
      pub_->publish(msg);
    });
    srv_ = create_service<SetBool>("counter/enable",
      [this](const SetBool::Request::SharedPtr req, SetBool::Response::SharedPtr res) {
        enabled_ = req->data;
        res->success = true;
        res->message = enabled_ ? "started" : "stopped";
        RCLCPP_INFO(get_logger(), "%s", res->message.c_str());
      });
  }

private:
  bool enabled_ = true;
  uint32_t count_ = 0;
  rclcpp::Publisher<UInt32>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Service<SetBool>::SharedPtr srv_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<CounterPub>());
  rclcpp::shutdown();
}
