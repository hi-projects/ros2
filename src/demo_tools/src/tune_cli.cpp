// =============================================================================
//  tune_cli —— 一个**不是 ROS 节点**的可执行文件
//
//  它故意不 include <rclcpp/rclcpp.hpp>，不写 rclcpp::init()，不建 Node，
//  不发布不订阅。它就是一个普通 C++ 程序，只是恰好放在 ROS2 工作空间里。
//
//  为什么要在 ROS2 工程里放这么一个东西？两个理由：
//
//   1) 它是这个 demo 的"最小验证"。不用起 DDS、不用 ros2 run、
//      不用 source setup.bash，直接跑就能看到 PID + 自动整定的效果。
//      后面 ROS 节点出问题时，你可以靠它快速区分
//      "是算法错了" 还是 "是 ROS 接线错了"。
//
//   2) 它是理解 ament_cmake 的一把钥匙：
//      「一个 ament_cmake 包完全可以产出非 ROS 的东西」。
//      ament_cmake 就是 CMake 加一层宏，add_executable 还是那个 add_executable。
//
//  编译完之后，这个可执行文件在：
//      install/demo_tools/lib/demo_tools/tune_cli
// =============================================================================

#include <cmath>
#include <cstdio>

#include <demo_math/pid.hpp>            // ← 来自 demo_math 包
#include <demo_tools/auto_tuner.hpp>    // ← 来自本包
#include <rclcpp/duration.hpp>          // ← 来自 rclcpp 包

int main()
{
  // 仿真参数
  constexpr double kDt = 0.02;    // 采样周期 20ms
  constexpr double kTau = 0.8;    // 被控对象时间常数（一阶惯性环节）
  constexpr int kSteps = 1600;    // 32 秒

  const auto dt = rclcpp::Duration::from_seconds(kDt);

  // 故意从一个很差的初始参数开始，好让 AutoTuner 有活干
  demo_math::PidGains gains;
  gains.kp = 0.30;
  gains.ki = 0.00;
  gains.kd = 0.00;

  demo_math::Pid pid(gains, /*output_limit=*/ 8.0);

  demo_tools::TuneConfig cfg;
  cfg.window = 50;              // 1 秒的观察窗口
  cfg.error_threshold = 0.15;
  cfg.oscillation_threshold = 8;

  demo_tools::AutoTuner tuner(pid, cfg);

  std::printf("=== demo_tools / tune_cli ===\n");
  std::printf("初始: %s\n", pid.describe().c_str());
  std::printf("%s\n", tuner.describe().c_str());
  std::printf("\n");
  std::printf("    t(s)   setpoint   measured    output      kp      ki      kd   note\n");
  std::printf("  ------  ---------  ---------  --------  ------  ------  ------  ----\n");

  double measured = 0.0;
  double setpoint = 0.0;
  int tune_events = 0;

  for (int i = 0; i < kSteps; ++i) {
    const double t = i * kDt;

    // 两段阶跃，第二段让控制器反向调节，顺便考验积分项
    if (i == 100) {setpoint = 5.0;}
    if (i == 900) {setpoint = 2.0;}

    const double output = pid.update(setpoint, measured, dt);

    // 一阶惯性对象：y' = (u - y) / tau
    measured += (output - measured) * kDt / kTau;

    const double error = setpoint - measured;
    const bool tuned = tuner.update(error, dt);
    if (tuned) {
      ++tune_events;
    }

    if (i % 100 == 0 || tuned) {
      const auto & g = pid.gains();
      std::printf(
        "  %6.2f  %9.3f  %9.3f  %8.3f  %6.3f  %6.3f  %6.3f  %s\n",
        t, setpoint, measured, output, g.kp, g.ki, g.kd,
        tuned ? "<-- 调整增益" : "");
    }
  }

  std::printf("\n结束: %s\n", pid.describe().c_str());
  std::printf("整定共触发 %d 次，累计样本 %zu 个\n",
    tune_events, tuner.samples());
  std::printf("\n提示：把上面的 kp/ki 填进 demo_nodes/config/params.yaml，\n");
  std::printf("      再 ros2 launch demo_nodes demo.launch.py，就能看到同样的响应。\n");
  return 0;
}
