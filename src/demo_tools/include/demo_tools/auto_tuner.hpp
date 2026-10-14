// =============================================================================
//  demo_tools —— 既是消费者，又是提供者
//
//  它同时演示两件事：
//    1) 作为一个包，怎么**用**别人的库   (demo_math)
//    2) 作为一个包，怎么**给别人用**     (自己的 demo_tools)
//
//  ★ 这个头文件在 demo_math 的依赖关系里扮演了决定性角色，看下面：
// =============================================================================
#ifndef DEMO_TOOLS__AUTO_TUNER_HPP_
#define DEMO_TOOLS__AUTO_TUNER_HPP_

#include <cstddef>
#include <memory>
#include <string>

// ★★ 为什么这一行决定了 demo_math 必须是 PUBLIC 依赖？★★
//
// AutoTuner 的构造函数接收一个 demo_math::Pid&。也就是说：
// **任何想用 AutoTuner 的人，都必须先能看见 demo_math::Pid 这个类型。**
//
// 于是 demo_math 不只是 demo_tools 的"实现细节"，而是它**公开接口的一部分**。
// 对应到 CMakeLists.txt 里就是：
//
//     target_link_libraries(demo_tools PUBLIC demo_math::demo_math)
//                                        ^^^^^^
// 而不能是 PRIVATE。写成 PRIVATE 的后果：
//   - demo_tools 自己编译没问题
//   - 下游（demo_nodes）编译时报 "unknown type name 'demo_math'"
//   - 而且报错位置在下游，你会以为是下游写错了 —— 这就是最难查的那类 bug
//
// 判断标准就一句话：
//     第三方类型出现在**公开头文件**里 -> PUBLIC
//     只出现在 .cpp 里                 -> PRIVATE
#include <demo_math/pid.hpp>

#include <rclcpp/duration.hpp>

namespace demo_tools
{

/// 自动整定参数。
///
/// 策略很朴素，够用就行：
///   - 窗口内误差符号频繁翻转（说明在振荡）-> 减小 kp、增大 kd
///   - 窗口内平均绝对误差一直很大（说明响应太慢）-> 增大 kp、小幅增大 ki
struct TuneConfig
{
  std::size_t window = 50;              ///< 观察窗口长度（采样点数）
  double error_threshold = 0.25;        ///< 平均绝对误差超过它就算"响应太慢"
  std::size_t oscillation_threshold = 8;///< 窗口内符号翻转次数超过它就算"振荡"
  double kp_step = 0.05;
  double ki_step = 0.01;
  double kd_step = 0.02;
  double kp_max = 3.0;
  double ki_max = 1.5;
  double kd_max = 0.5;
};

/// 一个会自己调参的 PID 外挂。
///
/// 它不拥有 Pid 对象，只持有一个引用，直接改它的增益。
class AutoTuner
{
public:
  explicit AutoTuner(demo_math::Pid & pid, const TuneConfig & cfg = TuneConfig());

  /// ★ pimpl 惯用法：Impl 的定义在 .cpp 里，所以内部的 RingBuffer 类型
  ///   **不会**出现在这个公开头文件里。
  ///   这就是那个内部静态库可以 PRIVATE 链接的**根本原因** ——
  ///   不是我们想藏着，而是接口设计上它本来就不该露出来。
  ///   如果 Impl 写在这里，RingBuffer 的头文件就必须也 install 出去，
  ///   那个库也就只能 PUBLIC 链接了。
  ~AutoTuner();

  AutoTuner(const AutoTuner &) = delete;
  AutoTuner & operator=(const AutoTuner &) = delete;

  /// 推入一拍误差。返回 true 表示这一拍真的调整了增益（方便调用方打日志）。
  bool update(double error, const rclcpp::Duration & dt);

  /// 已经累计喂进来的样本数
  std::size_t samples() const;

  /// 当前使用的整定配置
  const TuneConfig & config() const;

  std::string describe() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace demo_tools

#endif  // DEMO_TOOLS__AUTO_TUNER_HPP_
