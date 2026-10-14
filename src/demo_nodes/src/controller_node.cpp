// =============================================================================
//  controller_node —— 闭环控制器
//
//  它同时扮演了两个角色（真实系统里这两者是分开的，这里为了 demo 简单合体）：
//    1) 被控对象：一个一阶惯性环节 y' = (u - y) / tau  （真实系统里应是另一个节点）
//    2) 控制器：PID，每 period_ms 算一次输出
//
//  对外接口：
//    发布  <topic>              demo_interfaces/msg/Target
//    服务  <node>/reset         demo_interfaces/srv/Reset
//
//  ★ 注意这个文件里的 include：
//        #include <demo_math/pid.hpp>
//    但 demo_nodes/CMakeLists.txt 里**没有** find_package(demo_math)。
//    这不是写漏了，是故意的 —— 详见 demo_nodes/CMakeLists.txt 的注释。
// =============================================================================

#include <chrono>
#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include <demo_interfaces/msg/target.hpp>
#include <demo_interfaces/srv/reset.hpp>
#include <demo_math/pid.hpp>
#include <demo_tools/auto_tuner.hpp>

using namespace std::chrono_literals;

class ControllerNode : public rclcpp::Node
{
public:
  ControllerNode()
  : rclcpp::Node("controller")
  {
    // -------------------------------------------------------------------------
    // 参数声明。
    // declare_parameter 的三个作用（很多人只当成"读配置"）：
    //   1) 若 YAML / 命令行提供了值，用提供的；否则用默认值
    //   2) 把参数名登记到节点上，于是 `ros2 param list` 能看到它
    //   3) 校验类型 —— 声明成 double 而 YAML 里写整数 1，启动时直接报错
    //
    // ★ 类型要写死：用 declare_parameter<int64_t>() 这种显式模板参数，
    //   不要靠 20 这种字面量去推导。ROS2 内部整数一律是 int64，
    //   推导成 32 位 int 在跨平台时会出问题。
    // -------------------------------------------------------------------------
    kp_ = declare_parameter<double>("kp", 1.2);
    ki_ = declare_parameter<double>("ki", 0.35);
    kd_ = declare_parameter<double>("kd", 0.05);

    setpoint_ = declare_parameter<double>("setpoint", 5.0);
    output_limit_ = declare_parameter<double>("output_limit", 8.0);
    plant_tau_ = declare_parameter<double>("plant_tau", 0.8);

    const int64_t period_ms = declare_parameter<int64_t>("period_ms", 20);
    auto_tune_ = declare_parameter<bool>("auto_tune", true);
    const auto topic = declare_parameter<std::string>("topic", "target");

    const int64_t tune_window = declare_parameter<int64_t>("tune_window", 50);

    RCLCPP_INFO(get_logger(), "被控对象: 一阶惯性, tau=%.3f s", plant_tau_);
    RCLCPP_INFO(get_logger(), "控制周期: %ld ms, 设定值 %.3f", period_ms, setpoint_);

    // -------------------------------------------------------------------------
    // 构造 PID。注意这里是 unique_ptr 而不是成员对象：
    // 因为 Node 基类先构造、参数只能在函数体里读，
    // 而 Pid 的构造函数需要参数。这是个很常见的 CMake 无关但 C++ 相关的坑。
    // -------------------------------------------------------------------------
    pid_ = std::make_unique<demo_math::Pid>(
      demo_math::PidGains{kp_, ki_, kd_}, output_limit_);
    RCLCPP_INFO(get_logger(), "PID: %s", pid_->describe().c_str());

    if (auto_tune_) {
      demo_tools::TuneConfig cfg;
      cfg.window = static_cast<std::size_t>(tune_window);
      tuner_ = std::make_unique<demo_tools::AutoTuner>(*pid_, cfg);
      RCLCPP_INFO(get_logger(), "%s", tuner_->describe().c_str());
    }

    // -------------------------------------------------------------------------
    // 发布者 / 服务端 / 定时器
    // 主题名用相对名 "target" 而不是 "/target"：
    // 相对名会被解析成 <namespace>/target，于是可以通过 launch 的 namespace
    // 或 remap 改变它，而绝对名写死了改不动。ROS2 里除非有特殊理由，都用相对名。
    // -------------------------------------------------------------------------
    pub_ = create_publisher<demo_interfaces::msg::Target>(topic, 10);

    srv_ = create_service<demo_interfaces::srv::Reset>(
      "reset",
      std::bind(
        &ControllerNode::onReset, this,
        std::placeholders::_1, std::placeholders::_2));

    last_time_ = now();
    timer_ = create_wall_timer(
      std::chrono::milliseconds(period_ms),
      std::bind(&ControllerNode::onTimer, this));

    RCLCPP_INFO(get_logger(), "已启动: 发布 <%s>, 服务 <reset>", topic.c_str());
  }

private:
  void onTimer()
  {
    const rclcpp::Time stamp = now();

    // ★ dt 用"上一拍到这一拍"的实测时间差，而不是直接用 period_ms。
    //   因为定时器会抖动。PID 的积分项和微分项都对时间尺度敏感，
    //   用标称周期在长时间运行后会累积出可观误差。
    //   这也是 demo_math 的接口收 rclcpp::Duration 而不是 double 的原因。
    const rclcpp::Duration dt = stamp - last_time_;
    last_time_ = stamp;

    const double error = setpoint_ - measured_;
    const double output = pid_->update(setpoint_, measured_, dt);

    // 被控对象仿真：一阶惯性环节
    measured_ += (output - measured_) * dt.seconds() / plant_tau_;

    // 自动整定。真改了增益就打一条日志，方便和 tune_cli 的输出对照。
    if (tuner_ && tuner_->update(error, dt)) {
      RCLCPP_WARN(get_logger(), "自动整定触发 -> %s", pid_->describe().c_str());
    }

    demo_interfaces::msg::Target msg;
    msg.header.stamp = stamp;
    msg.header.frame_id = "sim";
    msg.setpoint = setpoint_;
    msg.measured = measured_;
    msg.error = error;
    msg.output = output;
    pub_->publish(msg);
  }

  void onReset(
    const std::shared_ptr<demo_interfaces::srv::Reset::Request> request,
    std::shared_ptr<demo_interfaces::srv::Reset::Response> response)
  {
    const double old = setpoint_;
    setpoint_ = request->new_setpoint;
    // 设定值变了，历史积分就没意义了，必须清掉，否则会有一次大的过冲
    pid_->reset();

    response->old_setpoint = old;
    response->success = true;

    RCLCPP_INFO(
      get_logger(), "reset: 设定值 %.3f -> %.3f",
      old, setpoint_);
  }

  // 参数
  double kp_{1.2};
  double ki_{0.35};
  double kd_{0.05};
  double setpoint_{5.0};
  double output_limit_{8.0};
  double plant_tau_{0.8};
  bool auto_tune_{true};

  // 状态
  double measured_{0.0};
  rclcpp::Time last_time_;

  std::unique_ptr<demo_math::Pid> pid_;
  std::unique_ptr<demo_tools::AutoTuner> tuner_;

  rclcpp::Publisher<demo_interfaces::msg::Target>::SharedPtr pub_;
  rclcpp::Service<demo_interfaces::srv::Reset>::SharedPtr srv_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ControllerNode>());
  rclcpp::shutdown();
  return 0;
}
