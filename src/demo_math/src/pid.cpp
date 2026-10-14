#include "demo_math/pid.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace demo_math
{

Pid::Pid(const PidGains & gains, double output_limit)
: gains_(gains), output_limit_(output_limit)
{
}

double Pid::update(double setpoint, double measured, const rclcpp::Duration & dt)
{
  const double error = setpoint - measured;
  const double dt_s = dt.seconds();

  // 微分项：第一拍没有历史，给 0，否则 (error - 0) / dt 会产生一个巨大的假冲击
  double derivative = 0.0;
  if (has_prev_error_ && dt_s > 0.0) {
    derivative = (error - prev_error_) / dt_s;
  }

  if (dt_s > 0.0) {
    integral_ += error * dt_s;
  }

  double output = gains_.kp * error + gains_.ki * integral_ + gains_.kd * derivative;

  // 抗积分饱和（anti-windup）：
  // 如果输出已经撞到限幅，就把这一拍刚积进去的量退回去。
  // 不退的话，只要误差还在，integral_ 就会一直涨；等误差反向后，
  // 需要很久才能把这个"虚假的"积分值消耗掉 —— 这就是积分饱和。
  if (output_limit_ > 0.0 && std::abs(output) > output_limit_) {
    if (dt_s > 0.0) {
      integral_ -= error * dt_s;
    }
    output = gains_.kp * error + gains_.ki * integral_ + gains_.kd * derivative;
    output = std::clamp(output, -output_limit_, output_limit_);
  }

  prev_error_ = error;
  has_prev_error_ = true;
  return output;
}

void Pid::reset()
{
  integral_ = 0.0;
  prev_error_ = 0.0;
  has_prev_error_ = false;
}

void Pid::setGains(const PidGains & gains)
{
  gains_ = gains;
}

const PidGains & Pid::gains() const
{
  return gains_;
}

double Pid::integral() const
{
  return integral_;
}

double Pid::outputLimit() const
{
  return output_limit_;
}

std::string Pid::describe() const
{
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss.precision(3);
  oss << "kp=" << gains_.kp << " ki=" << gains_.ki << " kd=" << gains_.kd
      << " limit=+/-" << output_limit_;
  return oss.str();
}

}  // namespace demo_math
