// =============================================================================
//  tune_client_node —— action 客户端
//
//  它给 controller 发一个"校准"目标，然后做三件事：
//    1) 目标被接受 / 被拒绝时，分别打印不同的话
//    2) 每收到一条反馈，打一行进度
//    3) 收到最终结果时，打印结论并退出
//
//  ★ 它存在的意义，就是把 action 相对于 service 多出来的三样东西演示全：
//
//        service 客户端        action 客户端
//        ─────────────         ─────────────
//        发请求                async_send_goal()
//        干等                    ↓ 先收到"目标被接受了吗"
//        （中间什么都看不到）   feedback_callback  ← ① 进度可见
//        收响应                  ↓ 中途还能 async_cancel_goal()  ← ② 可以叫停
//                              result_callback    ← ③ 明确的最终结论
//
//  ★ 注意这个文件**没有** rclcpp::spin 之外的控制流：
//    所有事情都是"回调驱动"的。这是 action 客户端的标准形态 ——
//    因为任务要跑很久，你不能在 main 里 while 等它。
//
//  跑法：
//      ros2 run demo_nodes tune_client_node
//  或者自己指定参数：
//      ros2 run demo_nodes tune_client_node --ros-args \
//        -p target:=3.0 -p cancel_after_s:=2.0
// =============================================================================

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include <demo_interfaces/action/calibrate.hpp>

using namespace std::chrono_literals;

using Calibrate = demo_interfaces::action::Calibrate;

// ★ 客户端这边的 handle 和 server 那边是两个不同的类，别混：
//    ClientGoalHandle —— 查状态、请求取消、拿结果
//    ServerGoalHandle —— 发反馈、宣告成功/失败/被取消
using GoalHandleCalibrate = rclcpp_action::ClientGoalHandle<Calibrate>;

namespace
{

/// 把 goal 的 UUID 缩写成 8 个十六进制字符
///
/// ★ 每个 goal 都有一个 16 字节的 UUID，由客户端在发送时生成（不是服务端给的）。
///   它存在的意义：一次任务从发送到结束，要经过好几个服务调用
///   （send_goal / feedback / get_result / cancel），全靠这个 id 串起来。
///   上面那张 §2.5 的表里，"goal_id" 就出现在每一个环节里。
std::string shortId(const rclcpp_action::GoalUUID & uuid)
{
  char buf[16];
  std::snprintf(
    buf, sizeof(buf), "%02x%02x%02x%02x",
    uuid[0], uuid[1], uuid[2], uuid[3]);
  return std::string(buf);
}

}  // namespace

class TuneClientNode : public rclcpp::Node
{
public:
  TuneClientNode()
  : rclcpp::Node("tune_client")
  {
    const auto action_name = declare_parameter<std::string>("action", "calibrate");

    target_ = declare_parameter<double>("target", 2.0);

    // ★ tolerance / hold_s / timeout_s 这三个数**必须一起定**，而且必须
    //   结合被控对象的快慢来定。这里给的默认值是实测出来的：
    //   本工程那个一阶惯性对象配这套 PID，有一个约 6 秒的慢主导极点，
    //   从误差 2.0 收敛到 0.1 要十几秒。
    //
    //   所以：
    //     tolerance 写 0.01  -> 任务几乎必然超时失败（不是代码错了，是物理上到不了）
    //     timeout_s 写 5.0   -> 同上
    //   想体验"失败也是一种正常结果"，就把 tolerance 改成 0.01 试试 ——
    //   你会看到 abort 分支和 SUCCEEDED / ABORTED 的区别。
    tolerance_ = declare_parameter<double>("tolerance", 0.1);
    hold_s_ = declare_parameter<double>("hold_s", 0.5);
    timeout_s_ = declare_parameter<double>("timeout_s", 30.0);

    // ★ 专为演示"取消"而设：> 0 时，发出目标后过这么多秒就把它取消掉。
    //   默认 0 = 不取消，走完整流程。
    cancel_after_s_ = declare_parameter<double>("cancel_after_s", 0.0);

    client_ = rclcpp_action::create_client<Calibrate>(this, action_name);

    // ★ 客户端和服务端之间**没有**启动顺序保证。谁先起都有可能。
    //   action 不是"连上一条线"，而是"去发现一堆话题和服务"：
    //   _send_goal / _get_result / _cancel 三个服务，_feedback / _status 两个话题。
    //   这些名字在图上齐了，才算"服务端可用"。
    //   下面这一步就是等它们齐活 —— 不加的话，第一发 goal 会石沉大海。
    //   10 秒是个经验值：DDS 发现机制本身就要几秒，再加上 launch 里
    //   几个节点是并行起来的，等太短会偶发失败（而且看起来像"代码有 bug"）。
    RCLCPP_INFO(get_logger(), "等待 action 服务端 <%s> ...", action_name.c_str());
    if (!client_->wait_for_action_server(10s)) {
      RCLCPP_ERROR(
        get_logger(),
        "等不到 <%s>。先起 controller 节点："
        "ros2 run demo_nodes controller_node", action_name.c_str());
      // ★ 构造函数里没法 return 错误码，用一个标志位让 main 决定退出码。
      //   （也可以在这里直接 rclcpp::shutdown()，但那样只是"退出"，
      //    调用方 ros2 run 看到的是成功码 —— 脚本里会很坑。）
      failed_ = true;
      return;
    }

    sendGoal();
  }

  /// 构造函数里没能发出去目标（比如等不到服务端）时，main 靠它决定退出码
  bool failed() const {return failed_;}

  /// 任务结束时留下的退出码，给 main 用
  int exitCode() const {return exit_code_;}

private:
  void sendGoal()
  {
    auto goal = Calibrate::Goal();
    goal.target = target_;
    goal.tolerance = tolerance_;
    goal.hold_s = hold_s_;
    goal.timeout_s = timeout_s_;

    rclcpp_action::Client<Calibrate>::SendGoalOptions options;

    // ★★ 三个回调，对应 action 生命周期里的三个时刻。
    //    这是 action 客户端和 service 客户端在写法上最大的不同：
    //    service 只给你一个 future，你得自己组织"等待"；
    //    action 直接把三个时刻的钩子摆在你面前。
    options.goal_response_callback =
      std::bind(&TuneClientNode::onGoalResponse, this, std::placeholders::_1);
    options.feedback_callback =
      std::bind(
      &TuneClientNode::onFeedback, this,
      std::placeholders::_1, std::placeholders::_2);
    options.result_callback =
      std::bind(&TuneClientNode::onResult, this, std::placeholders::_1);

    RCLCPP_INFO(
      get_logger(), "发送目标: target=%.3f tol=%.3f hold=%.2fs timeout=%.2fs",
      goal.target, goal.tolerance, goal.hold_s, goal.timeout_s);

    std::printf(
      "\n   goal_id   t(s)      value      error   progress  耗时\n"
      "   --------  -------  ---------  ---------  --------  ------\n");

    // ★ async_send_goal 返回一个 future，但**不要**去 .get() 它 ——
    //   那会阻塞到目标被接受为止，整个回调体系就白设计了。
    //   接受/拒绝的结果会走 goal_response_callback。
    client_->async_send_goal(goal, options);

    // 需要在半路取消的话，起一个一次性定时器
    if (cancel_after_s_ > 0.0) {
      cancel_timer_ = create_wall_timer(
        std::chrono::duration<double>(cancel_after_s_),
        std::bind(&TuneClientNode::requestCancel, this));
      RCLCPP_INFO(get_logger(), "将在 %.2fs 后请求取消（演示用）", cancel_after_s_);
    }
  }

  /// ① 目标被接受还是被拒绝
  void onGoalResponse(GoalHandleCalibrate::SharedPtr goal_handle)
  {
    if (!goal_handle) {
      // ★ 服务端返回 REJECT 时，这里收到的是空指针。
      //   controller 在"已有任务在执行"或"参数不合法"时会拒绝。
      RCLCPP_ERROR(get_logger(), "目标被拒绝");
      finish(1);
      return;
    }
    goal_handle_ = goal_handle;
    goal_id_ = shortId(goal_handle->get_goal_id());
    RCLCPP_INFO(get_logger(), "目标已接受，goal_id=%s", goal_id_.c_str());
  }

  /// ② 收到一条进度反馈
  void onFeedback(
    GoalHandleCalibrate::SharedPtr,
    const std::shared_ptr<const Calibrate::Feedback> feedback)
  {
    ++feedback_count_;
    std::printf(
      "   %s  %7.3f  %9.4f  %9.4f  %7.1f%%\n",
      goal_id_.c_str(), feedback->elapsed_s,
      feedback->current_value, feedback->current_error,
      feedback->progress * 100.0);
  }

  /// ③ 收到最终结果
  void onResult(const GoalHandleCalibrate::WrappedResult & result)
  {
    // ★ result.code 和 result.result 是两码事，这个区分很重要：
    //
    //     result.code    任务**怎么结束的**（枚举，框架层）
    //     result.result  你自己在 .action 里定义的 Result 消息
    //
    //   服务端还可能因为"目标被取消后客户端才来取结果"之类的原因，
    //   让你拿到的 result.result 是空的 —— 所以**先判 code，再读字段**。
    std::printf("\n");
    switch (result.code) {
      case rclcpp_action::ResultCode::SUCCEEDED:
        std::printf("   结果: 成功\n");
        break;
      case rclcpp_action::ResultCode::ABORTED:
        std::printf("   结果: 失败（服务端主动放弃）\n");
        break;
      case rclcpp_action::ResultCode::CANCELED:
        std::printf("   结果: 已取消\n");
        break;
      default:
        std::printf("   结果: 未知\n");
        break;
    }

    if (result.result) {
      std::printf(
        "   success=%d  final_value=%.4f  final_error=%.4f  耗时=%.2fs\n"
        "   说明: %s\n",
        result.result->success ? 1 : 0,
        result.result->final_value, result.result->final_error,
        result.result->elapsed_s, result.result->message.c_str());
    }

    std::printf(
      "   收到 %zu 条反馈;  用 `ros2 action info` 可以看到这次任务在网络上的全貌\n",
      feedback_count_);

    finish(result.code == rclcpp_action::ResultCode::SUCCEEDED ? 0 : 2);
  }

  /// 中途请求取消（由 cancel_timer_ 触发）
  void requestCancel()
  {
    // 一次性定时器：触发完就取消掉，免得又跑一遍
    cancel_timer_->cancel();

    if (!goal_handle_) {
      return;   // 目标还没被接受，或者已经被拒了
    }

    RCLCPP_WARN(get_logger(), "请求取消 goal_id=%s", goal_id_.c_str());

    // ★ 注意这里是"请求"。服务端收到后要先回一句"让不让取消",
    //   然后还要走完自己的收尾流程（发 Result）。所以客户端这边
    //   **不是**调完就结束了 —— 真正的结束仍然由 onResult 收到。
    //   一堆人第一次用 action 时会在这里等下一条日志，结果什么都没打印，
    //   以为取消失灵了。其实服务端在下一拍（20ms 后）就回话了。
    client_->async_cancel_goal(goal_handle_);
  }

  void finish(int exit_code)
  {
    exit_code_ = exit_code;
    // ★ 在回调里调用 shutdown() 是标准做法：让 spin 退出，main 收尾。
    //   不要试图在回调里 delete 节点或者做更重的事。
    rclcpp::shutdown();
  }

  double target_{2.0};
  double tolerance_{0.05};
  double hold_s_{0.5};
  double timeout_s_{15.0};
  double cancel_after_s_{0.0};

  bool failed_{false};
  int exit_code_{0};
  std::size_t feedback_count_{0};
  std::string goal_id_;

  rclcpp_action::Client<Calibrate>::SharedPtr client_;
  GoalHandleCalibrate::SharedPtr goal_handle_;
  rclcpp::TimerBase::SharedPtr cancel_timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<TuneClientNode>();
  if (node->failed()) {
    rclcpp::shutdown();
    return 1;
  }

  rclcpp::spin(node);
  rclcpp::shutdown();

  // ★ 把任务结局翻译成退出码，脚本里就能这样用：
  //       ros2 run demo_nodes tune_client_node && echo "校准成功"
  //   0=成功  1=目标被拒绝/连不上服务端  2=失败(超时)或被取消
  return node->exitCode();
}
