// =============================================================================
//  monitor_node —— 订阅者 / 监视器
//
//  订阅 controller 发的 Target 消息，每隔 print_every 帧打印一行，
//  并统计最大绝对误差和平均绝对误差。
//
//  它存在的意义：让你能亲眼看到"发布端改了参数，订阅端的数据就变了"，
//  从而确认整条链路（msg 代码生成 -> 库 -> 节点 -> DDS）是通的。
// =============================================================================

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include <demo_interfaces/msg/target.hpp>

class MonitorNode : public rclcpp::Node
{
public:
  MonitorNode()
  : rclcpp::Node("monitor")
  {
    const auto topic = declare_parameter<std::string>("topic", "target");
    const int64_t print_every = declare_parameter<int64_t>("print_every", 25);
    print_every_ = (print_every > 0) ? static_cast<std::size_t>(print_every) : 1;

    sub_ = create_subscription<demo_interfaces::msg::Target>(
      topic, 10,
      std::bind(&MonitorNode::onMsg, this, std::placeholders::_1));

    std::printf(
      "\n    #      t(s)   setpoint   measured      error     output\n"
      "  -----  --------  ---------  ---------  ---------  ---------\n");

    RCLCPP_INFO(get_logger(), "已订阅 <%s>, 每 %zu 帧打印一次",
      topic.c_str(), print_every_);
  }

private:
  void onMsg(const demo_interfaces::msg::Target::SharedPtr msg)
  {
    ++count_;

    const double abs_err = std::abs(msg->error);
    if (abs_err > max_abs_error_) {
      max_abs_error_ = abs_err;
    }
    sum_abs_error_ += abs_err;

    // 前 3 帧必打（让你立刻看到数据通了），之后按节流打印
    if (count_ <= 3 || count_ % print_every_ == 0) {
      const double t = msg->header.stamp.sec + msg->header.stamp.nanosec * 1e-9;
      std::printf(
        "  %5zu  %8.3f  %9.3f  %9.3f  %9.3f  %9.3f\n",
        count_, t, msg->setpoint, msg->measured, msg->error, msg->output);
    }

    // 每 500 帧打一次汇总
    if (count_ % 500 == 0) {
      std::printf(
        "  ----- 累计 %zu 帧: 最大|误差|=%.4f  平均|误差|=%.4f\n",
        count_, max_abs_error_,
        sum_abs_error_ / static_cast<double>(count_));
    }
  }

  std::size_t count_{0};
  std::size_t print_every_{25};
  double max_abs_error_{0.0};
  double sum_abs_error_{0.0};

  rclcpp::Subscription<demo_interfaces::msg::Target>::SharedPtr sub_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MonitorNode>());
  rclcpp::shutdown();
  return 0;
}
