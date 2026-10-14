// =============================================================================
//  内部实现细节 —— 这个头文件**不会**被 install
//
//  它只存在于源码树里（src/internal/），被 auto_tuner.cpp 使用。
//  外面的包永远看不到它，因为它没有出现在任何公开头文件里。
//
//  对应到 CMakeLists.txt：
//    - 它被编成一个独立的静态库 demo_tools_internal
//    - 那个库被 PRIVATE 链接进 demo_tools
//    - 那个库没有 install()，也没有进任何 EXPORT 集合
//
//  ★ 这就是 INTERFACE / PUBLIC / PRIVATE 三者里 PRIVATE 的真实使用场景。
//    不是为了"优化"，而是为了表达一个设计事实：这是实现细节。
// =============================================================================
#ifndef DEMO_TOOLS__INTERNAL__RING_BUFFER_HPP_
#define DEMO_TOOLS__INTERNAL__RING_BUFFER_HPP_

#include <cstddef>
#include <vector>

namespace demo_tools
{
namespace internal
{

/// 定长环形缓冲区，给 AutoTuner 做滑动窗口统计用。
class RingBuffer
{
public:
  explicit RingBuffer(std::size_t capacity);

  void push(double value);
  void clear();

  bool full() const;
  std::size_t size() const;
  std::size_t capacity() const;

  /// 窗口内所有值的平均绝对值
  double meanAbs() const;

  /// 窗口内相邻两个值符号不同的次数（按时间顺序，最老的在前）
  std::size_t signFlips() const;

private:
  /// 把环形缓冲按"从老到新"的顺序拷成一个线性序列
  std::vector<double> chronological() const;

  std::vector<double> buf_;
  std::size_t capacity_;
  std::size_t head_;   ///< 下一个要写入的位置
  std::size_t count_;  ///< 当前已有元素个数
};

}  // namespace internal
}  // namespace demo_tools

#endif  // DEMO_TOOLS__INTERNAL__RING_BUFFER_HPP_
