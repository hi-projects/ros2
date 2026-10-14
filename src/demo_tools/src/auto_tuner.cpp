#include "demo_tools/auto_tuner.hpp"

#include <algorithm>
#include <sstream>

// ★ 注意这个 include 的写法："internal/ring_buffer.hpp" 而不是
//   "demo_tools/internal/ring_buffer.hpp"。
//   因为 demo_tools_internal 这个库把自己的 include 目录设成了 src/，
//   而那个目录**从来不会**被 install 出去。
//   下游连这个文件的路径都不知道，更别说 include 了。
#include "internal/ring_buffer.hpp"

namespace demo_tools
{

// ---------------------------------------------------------------------------
//  Impl —— pimpl 的真正实现。
//  这里是整个工程里唯一使用 demo_tools::internal::RingBuffer 的地方。
// ---------------------------------------------------------------------------
class AutoTuner::Impl
{
public:
  Impl(demo_math::Pid & pid, const TuneConfig & cfg)
  : pid_(pid), cfg_(cfg), window_(cfg.window), samples_(0)
  {
  }

  demo_math::Pid & pid_;
  TuneConfig cfg_;
  internal::RingBuffer window_;
  std::size_t samples_;

  bool update(double error)
  {
    ++samples_;
    window_.push(error);

    // 窗口没攒满就先不动，避免用几个点就下结论
    if (!window_.full()) {
      return false;
    }

    const double mean_abs = window_.meanAbs();
    const std::size_t flips = window_.signFlips();

    demo_math::PidGains gains = pid_.gains();
    bool changed = false;

    if (flips >= cfg_.oscillation_threshold) {
      // 诊断：在振荡。比例增益放小一点，微分项加一点阻尼
      gains.kp = std::max(gains.kp * 0.9, 0.05);
      gains.kd = std::min(gains.kd + cfg_.kd_step, cfg_.kd_max);
      changed = true;
    } else if (mean_abs > cfg_.error_threshold) {
      // 诊断：收敛太慢。加大比例，积分也给一点
      gains.kp = std::min(gains.kp + cfg_.kp_step, cfg_.kp_max);
      gains.ki = std::min(gains.ki + cfg_.ki_step, cfg_.ki_max);
      changed = true;
    }

    if (changed) {
      pid_.setGains(gains);
      // 改完参数要重新观察，否则每一拍都判"还在振荡"，一路把 kp 压到 0
      window_.clear();
    }
    return changed;
  }
};

AutoTuner::AutoTuner(demo_math::Pid & pid, const TuneConfig & cfg)
: impl_(std::make_unique<Impl>(pid, cfg))
{
}

// 析构函数必须写在 .cpp 里。因为 Impl 在这里才是完整类型；
// 写在头文件里的话（哪怕是 = default），编译器在别处销毁 unique_ptr 时
// 需要完整类型，会报 "invalid application of sizeof to incomplete type"。
AutoTuner::~AutoTuner() = default;

bool AutoTuner::update(double error, const rclcpp::Duration & dt)
{
  // dt 暂时没用上，但保留在接口里：真实的整定算法需要按时间尺度归一化，
  // 否则采样周期一变，阈值就全失效了。
  (void)dt;
  return impl_->update(error);
}

std::size_t AutoTuner::samples() const
{
  return impl_->samples_;
}

const TuneConfig & AutoTuner::config() const
{
  return impl_->cfg_;
}

std::string AutoTuner::describe() const
{
  std::ostringstream oss;
  oss << "AutoTuner(window=" << impl_->cfg_.window
      << ", osc_threshold=" << impl_->cfg_.oscillation_threshold
      << ", err_threshold=" << impl_->cfg_.error_threshold
      << ", samples=" << impl_->samples_ << ")";
  return oss.str();
}

}  // namespace demo_tools
