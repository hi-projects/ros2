#include "internal/ring_buffer.hpp"

#include <cmath>

namespace demo_tools
{
namespace internal
{

RingBuffer::RingBuffer(std::size_t capacity)
: buf_(capacity, 0.0), capacity_(capacity), head_(0), count_(0)
{
}

void RingBuffer::push(double value)
{
  if (capacity_ == 0) {
    return;
  }
  buf_[head_] = value;
  head_ = (head_ + 1) % capacity_;
  if (count_ < capacity_) {
    ++count_;
  }
}

void RingBuffer::clear()
{
  head_ = 0;
  count_ = 0;
}

bool RingBuffer::full() const
{
  return count_ == capacity_ && capacity_ > 0;
}

std::size_t RingBuffer::size() const
{
  return count_;
}

std::size_t RingBuffer::capacity() const
{
  return capacity_;
}

std::vector<double> RingBuffer::chronological() const
{
  std::vector<double> out;
  out.reserve(count_);
  // head_ 指向下一个写入位置，也就是最老的那个元素所在处
  const std::size_t start = (count_ == capacity_) ? head_ : 0;
  for (std::size_t i = 0; i < count_; ++i) {
    out.push_back(buf_[(start + i) % capacity_]);
  }
  return out;
}

double RingBuffer::meanAbs() const
{
  if (count_ == 0) {
    return 0.0;
  }
  const auto values = chronological();
  double sum = 0.0;
  for (double v : values) {
    sum += std::abs(v);
  }
  return sum / static_cast<double>(count_);
}

std::size_t RingBuffer::signFlips() const
{
  if (count_ < 2) {
    return 0;
  }
  const auto values = chronological();
  std::size_t flips = 0;
  for (std::size_t i = 1; i < values.size(); ++i) {
    // 允许误差在 0 附近抖动，用一个小死区避免把噪声当成真翻转
    constexpr double kEps = 1e-3;
    if (std::abs(values[i]) < kEps || std::abs(values[i - 1]) < kEps) {
      continue;
    }
    if ((values[i] > 0.0) != (values[i - 1] > 0.0)) {
      ++flips;
    }
  }
  return flips;
}

}  // namespace internal
}  // namespace demo_tools
