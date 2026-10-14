// =============================================================================
//  demo_math —— 一个正经的 ROS2 C++ 库包（不含任何节点）
//
//  这个头文件是**公开接口**：它会被 install 到 install/demo_math/include/，
//  别的包能 #include 它。所以它里面出现的一切类型，都成了本包的"公开依赖"。
//
//  ★ 注意这个 include：
// =============================================================================
#ifndef DEMO_MATH__PID_HPP_
#define DEMO_MATH__PID_HPP_

#include <string>

// ★★ 这一行是整个 demo 里最重要的设计决策 ★★
//
// 我们把 rclcpp::Duration 放进了**公开头文件**。后果：
//   1) 任何 include "demo_math/pid.hpp" 的包，都必须能 include 到 rclcpp/duration.hpp
//   2) 于是 demo_math 对 rclcpp 的依赖必须是 PUBLIC 的，不能是 PRIVATE
//   3) 于是 demo_math 必须 ament_export_dependencies(rclcpp)，
//      否则下游 find_package(demo_math) 时 CMake 会报
//      "the target rclcpp::rclcpp was not found"
//
// 如果 pid.hpp 里只用 double，不用 rclcpp::Duration，那么 rclcpp 就只是
// demo_math 的**实现细节**，可以 PRIVATE 链接、可以不导出。
// 一个类型出现在公开头文件里 = 一次依赖关系的公开承诺。这是 CMake 的核心思想。
#include <rclcpp/duration.hpp>

namespace demo_math
{

/// PID 三参数。用结构体而不是三个 double，是为了能整体传参、整体打印。
struct PidGains
{
  double kp = 1.0;   ///< 比例增益
  double ki = 0.0;   ///< 积分增益
  double kd = 0.0;   ///< 微分增益
};

/// 位置式 PID 控制器，带输出限幅和抗积分饱和。
///
/// 线程安全性：不是线程安全的，调用方自己保证串行调用。
class Pid
{
public:
  Pid() = default;

  /// @param output_limit 输出绝对值上限；<= 0 表示不限幅
  explicit Pid(const PidGains & gains, double output_limit = 10.0);

  /// 推进一拍，返回本拍的控制输出。
  /// @param dt 距离上一拍的时间间隔（由调用方用 rclcpp 的时钟算出）
  double update(double setpoint, double measured, const rclcpp::Duration & dt);

  void reset();
  void setGains(const PidGains & gains);
  const PidGains & gains() const;
  double integral() const;
  double outputLimit() const;

  /// 人类可读的一行描述，方便节点打印
  std::string describe() const;

private:
  PidGains gains_{};
  double integral_{0.0};
  double prev_error_{0.0};
  bool has_prev_error_{false};
  double output_limit_{10.0};
};

}  // namespace demo_math

#endif  // DEMO_MATH__PID_HPP_
