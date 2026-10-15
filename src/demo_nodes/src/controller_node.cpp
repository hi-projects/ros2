// =============================================================================
//  controller_node —— 闭环控制器
//
//  它同时扮演了两个角色（真实系统里这两者是分开的，这里为了 demo 简单合体）：
//    1) 被控对象：一个一阶惯性环节 y' = (u - y) / tau  （真实系统里应是另一个节点）
//    2) 控制器：PID，每 period_ms 算一次输出
//
//  对外接口：
//    发布  <topic>              demo_interfaces/msg/Target      （topic）
//    服务  <node>/reset         demo_interfaces/srv/Reset       （service）
//    动作  <node>/calibrate     demo_interfaces/action/Calibrate（action）
//
//  ★ 三种接口在这一个节点里各有一个，正好可以对比着看：
//      topic    —— 只管"一直往外说"，不关心谁在听，发出去就不管了
//      service  —— 一问一答，立刻返回；调用方要一直等到响应
//      action   —— 领一个任务，跑很久，期间不停汇报进度，还能被取消
//
//  ★ 注意这个文件里的 include：
//        #include <demo_math/pid.hpp>
//    但 demo_nodes/CMakeLists.txt 里**没有** find_package(demo_math)。
//    这不是写漏了，是故意的 —— 详见 demo_nodes/CMakeLists.txt 的注释。
// =============================================================================

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

// ★ 写 action 的 server / client 才需要这个头文件。
//   它对应 CMakeLists.txt 里的 find_package(rclcpp_action) ——
//   注意是 **rclcpp_action**，不是 action_msgs。
//   一个是"写节点的库"，一个是"生成的数据类型"，两者完全不是一回事。
#include <rclcpp_action/rclcpp_action.hpp>

#include <demo_interfaces/action/calibrate.hpp>
#include <demo_interfaces/msg/target.hpp>
#include <demo_interfaces/srv/reset.hpp>
#include <demo_math/pid.hpp>
#include <demo_tools/auto_tuner.hpp>

using namespace std::chrono_literals;

// -----------------------------------------------------------------------------
//  action 的两个类型别名。
//
//  ★ demo_interfaces::action::Calibrate 是一个**没有任何数据的空结构体**，
//    它只干一件事：用 using 把生成出来的那一堆类型串成一个"动作"。
//    真正有字段的是 Calibrate::Goal / Calibrate::Result / Calibrate::Feedback。
//    （去看 install/.../action/detail/calibrate__struct.hpp 最后 30 行。）
//
//  ★ ServerGoalHandle<ActionT> 是**服务端**这次任务的那张"任务单"：
//    它带着 goal_id、客户端发来的 Goal、以及 succeed()/abort()/canceled()
//    这三个"宣告任务结束"的方法。一个 goal 对应一个 handle。
//    客户端那边还有一个同名的 ClientGoalHandle，两边不是一回事。
// -----------------------------------------------------------------------------
using Calibrate = demo_interfaces::action::Calibrate;
using GoalHandleCalibrate = rclcpp_action::ServerGoalHandle<Calibrate>;

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

    // action 反馈的发送间隔。★ 反馈**不该**每一拍都发：
    // 控制周期是 20ms（50Hz），按 50Hz 发反馈会把这条话题压得比状态话题还重，
    // 而客户端大多只是拿来画曲线、打日志，10Hz 完全够。
    // "用一个独立的节流参数把反馈频率和计算频率解耦"是 action 的常见做法。
    const int64_t feedback_ms = declare_parameter<int64_t>("action_feedback_ms", 100);
    feedback_interval_s_ = static_cast<double>(feedback_ms) / 1000.0;

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
    //
    // 名字一律用相对名（"target"、"reset"、"calibrate"）而不是 "/target"：
    // 相对名能通过 launch 的 namespace 或 remap 改变，绝对名写死了改不动。
    // ROS2 里除非有特殊理由，都用相对名。
    //
    // ★★★ 但新手几乎一定会在这里踩一个坑，实测结果如下：
    //
    //     节点名 "controller"，相对名 "reset"
    //         -> 服务实际叫 **/reset**，不是 /controller/reset ！
    //
    //   原因：相对名是拼在**命名空间**（namespace）后面的，不是拼在节点名后面。
    //   本节点的命名空间是 "/"（没设过），所以 "reset" 就是 /reset。
    //
    //   刚才那两条命令实测结果：
    //       ros2 service call /controller/reset ...  -> 等不到服务，一直挂着
    //       ros2 service call /reset ...             -> 正常返回
    //
    //   ★ 那什么时候才会有 /controller/reset？两种写法：
    //       1) 名字写成 "~/reset"   —— "~" 是**节点私有命名空间**，展开成节点名
    //       2) 用 launch remap：'/reset': '/controller/reset'
    //     一般给"这个节点自己用的内部服务"加 ~ ，给"对外公开的接口"不加。
    //
    //   ★ 而最后一行 RCLCPP_INFO 里写的 <reset> / <calibrate> 是**相对名**，
    //     不是完整名 —— 日志里也别写死 /controller/reset，否则下次改
    //     namespace 时日志就开始骗人。要看真实名字，用：
    //         ros2 service list / ros2 action list / ros2 topic list
    // -------------------------------------------------------------------------
    pub_ = create_publisher<demo_interfaces::msg::Target>(topic, 10);

    srv_ = create_service<demo_interfaces::srv::Reset>(
      "reset",
      std::bind(
        &ControllerNode::onReset, this,
        std::placeholders::_1, std::placeholders::_2));

    // -------------------------------------------------------------------------
    // action 服务端。
    //
    // ★ 这一行背后，rclcpp_action 帮你建了**一堆**东西。
    //   它们全都藏在 `_action` 下面，默认是"隐藏"的，
    //   要用 --include-hidden-* 才看得到：
    //
    //     ros2 service list --include-hidden-services
    //       /calibrate/_action/send_goal     ← Calibrate_SendGoal 服务
    //       /calibrate/_action/get_result    ← Calibrate_GetResult 服务
    //       /calibrate/_action/cancel_goal   ← action_msgs/srv/CancelGoal（通用）
    //
    //     ros2 topic list --include-hidden-topics
    //       /calibrate/_action/feedback      ← Calibrate_FeedbackMessage 话题
    //       /calibrate/_action/status        ← action_msgs/msg/GoalStatusArray
    //
    //   ★ 3 个服务 + 2 个话题 —— 这就是一个 action 在网络上的**全部**。
    //     看不见任何"action"字样，因为它根本不是一种新的通信原语。
    //     这条命令建议真的敲一遍，比读十页文档都管用。
    //
    //   而 `ros2 node info /controller` 只会淡淡地写一行
    //       Action Servers:
    //         /calibrate: demo_interfaces/action/Calibrate
    //   把上面那 5 个实体藏得干干净净。
    //
    // ★ 三个回调缺一不可，而且**分工严格**：
    //
    //      handleGoal     收到新目标时问一句"接不接"（能不能立刻答完）
    //      handleCancel   收到取消请求时问一句"让不让取消"（能不能立刻答完）
    //      handleAccepted 目标被接受之后，把"这次任务"记下来（能不能立刻答完）
    //
    //   ★★ 这三个回调都必须是**立刻返回**的，绝对不能在里面干重活。
    //      原因和 service 一样：它们跑在 executor 的回调线程里，
    //      一阻塞，同一个节点上所有话题/服务/timer 全都停摆。
    //      真正的"干活"必须交给别的地方 —— 本例交给控制定时器（见 onTimer）。
    //      这就是 action 比 service 复杂的地方：它是个**状态机**，
    //      你得自己找地方存放"当前任务"，并在合适的时候推进它。
    // -------------------------------------------------------------------------
    action_server_ = rclcpp_action::create_server<Calibrate>(
      this,
      "calibrate",
      std::bind(
        &ControllerNode::handleGoal, this,
        std::placeholders::_1, std::placeholders::_2),
      std::bind(&ControllerNode::handleCancel, this, std::placeholders::_1),
      std::bind(&ControllerNode::handleAccepted, this, std::placeholders::_1));

    last_time_ = now();
    timer_ = create_wall_timer(
      std::chrono::milliseconds(period_ms),
      std::bind(&ControllerNode::onTimer, this));

    RCLCPP_INFO(
      get_logger(), "已启动: 发布 <%s>, 服务 <reset>, 动作 <calibrate>", topic.c_str());
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

    // ★ action 的"干活"就在这里推进。
    //   注意它是**搭在控制循环上**的，而不是自己开线程 —— 因为判断
    //   "校准完成了没有"本来就需要每一拍的数据。这也是 action 服务端
    //   最常见的写法：把任务状态机挂进已有的周期性回调里。
    updateCalibration(stamp, error, dt);
  }

  void onReset(
    const std::shared_ptr<demo_interfaces::srv::Reset::Request> request,
    std::shared_ptr<demo_interfaces::srv::Reset::Response> response)
  {
    // ★ 一个节点上同时挂着 service 和 action 时，"它们能不能同时用"
    //   是**你必须主动设计**的事，ROS2 不会替你管。
    //
    //   这里的选择是：校准期间拒绝改设定值。
    //   因为校准任务正在把被控量往 target 上赶，中途把 setpoint 换掉，
    //   任务会一直等不到收敛，最后只能超时失败 —— 对调用方来说，
    //   这比"当场被拒绝"要难理解得多。**明确地失败 > 莫名其妙地失败。**
    if (goal_handle_) {
      response->old_setpoint = setpoint_;
      response->success = false;
      RCLCPP_WARN(get_logger(), "reset 被拒绝：正在执行校准任务");
      return;
    }

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

  // =========================================================================
  //  action 服务端的三段式：goal / cancel / accepted
  // =========================================================================

  /// ① 收到新目标 —— 只回答"接不接"，不许在这里干活
  rclcpp_action::GoalResponse handleGoal(
    const rclcpp_action::GoalUUID & uuid,
    std::shared_ptr<const Calibrate::Goal> goal)
  {
    (void)uuid;   // goal 的 UUID 由框架生成，服务端一般用不上

    // ★ "同一时刻只允许一个任务"是服务端**自己的策略**，不是 action 的规则。
    //   action 协议本身允许一个客户端同时持有多个 goal。
    //   这里选择拒绝第二个：被控对象只有一个，两个校准目标打架没有意义。
    //   拒绝要用 REJECT —— 客户端会立刻收到"目标被拒"，而不是苦等超时。
    if (goal_handle_) {
      RCLCPP_WARN(
        get_logger(), "calibrate 目标被拒绝：已有任务在进行中 (target=%.3f)",
        goal->target);
      return rclcpp_action::GoalResponse::REJECT;
    }

    // 参数校验也在这一关做。发出去了才发现参数不合法，是最糟糕的接口设计。
    if (goal->tolerance <= 0.0 || goal->hold_s <= 0.0 || goal->timeout_s <= 0.0) {
      RCLCPP_WARN(
        get_logger(),
        "calibrate 目标被拒绝：参数不合法 (tolerance=%.3f hold_s=%.3f timeout_s=%.3f)",
        goal->tolerance, goal->hold_s, goal->timeout_s);
      return rclcpp_action::GoalResponse::REJECT;
    }

    RCLCPP_INFO(
      get_logger(), "calibrate 收到目标: target=%.3f tol=%.3f hold=%.2fs timeout=%.2fs",
      goal->target, goal->tolerance, goal->hold_s, goal->timeout_s);
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  /// ② 收到取消请求 —— 只回答"让不让取消"
  rclcpp_action::CancelResponse handleCancel(
    const std::shared_ptr<GoalHandleCalibrate> goal_handle)
  {
    // ★ 这里**只表态，不动手**。真正的收尾（调 canceled() 并回填 Result）
    //   留给 updateCalibration() 在下一拍做。
    //
    //   为什么不在这里直接 canceled()？因为这个回调可能和别的回调并发跑，
    //   而 canceled() 会去操作服务端内部的状态机和几个 topic/service。
    //   把它挪到唯一的"推进点"（定时器回调）里执行，
    //   整个任务状态就永远只有一个线程在改，不用加锁也不会打架。
    //   ★ 这条经验对所有 action 服务端都适用：**取消回调只做决策，不做收尾。**
    if (goal_handle_ && goal_handle->get_goal_id() == goal_handle_->get_goal_id()) {
      RCLCPP_INFO(get_logger(), "calibrate 收到取消请求，将在下一拍结束任务");
      return rclcpp_action::CancelResponse::ACCEPT;
    }
    RCLCPP_WARN(get_logger(), "calibrate 收到取消请求，但目标已经不在执行了");
    return rclcpp_action::CancelResponse::REJECT;
  }

  /// ③ 目标被接受，把"这次任务"记下来，开始执行
  void handleAccepted(const std::shared_ptr<GoalHandleCalibrate> goal_handle)
  {
    const auto goal = goal_handle->get_goal();

    goal_handle_ = goal_handle;
    goal_start_time_ = now();
    last_feedback_time_ = goal_start_time_;
    goal_hold_accum_s_ = 0.0;
    goal_initial_error_ = goal->target - measured_;

    // 校准 = 把设定值挪到目标上 + 清掉积分。
    // 这两件事和 onReset 干的一模一样 —— 因为本质上就是同一件事，
    // 只不过一个由 service 触发（瞬间完成），一个由 action 触发（要盯很久）。
    setpoint_ = goal->target;
    pid_->reset();

    // ★ 这里**不**调用 goal_handle->execute()。
    //   因为我们上面返回的是 ACCEPT_AND_EXECUTE，goal 已经是"执行中"了；
    //   再调一次 execute() 会因为"不是 pending 状态"直接抛异常。
    //   只有返回 ACCEPT（延迟执行）时才需要手动 execute()。
    //   这个坑很隐蔽：报错信息是 rcl 层的一句 "goal handle is not in pending
    //   state"，看起来跟你的代码毫无关系。
  }

  /// 任务推进 —— 每一拍调一次，这里是整个 action 唯一的状态修改点
  void updateCalibration(
    const rclcpp::Time & stamp, double error, const rclcpp::Duration & dt)
  {
    if (!goal_handle_) {
      return;
    }

    const auto goal = goal_handle_->get_goal();
    const double elapsed = (stamp - goal_start_time_).seconds();
    const double current_value = goal->target - error;

    // 结果先攒好。三种结局（取消 / 成功 / 超时）都要用它，只是字段不同。
    auto result = std::make_shared<Calibrate::Result>();
    result->final_value = current_value;
    result->final_error = error;
    result->elapsed_s = elapsed;

    // ---- 结局 1：客户端要取消 ----
    if (goal_handle_->is_canceling()) {
      result->success = false;
      result->message = "被客户端取消";
      goal_handle_->canceled(result);
      RCLCPP_WARN(get_logger(), "calibrate 已取消 (耗时 %.2fs)", elapsed);
      clearGoal();
      return;
    }

    // ---- 结局 2：稳住够久了，成功 ----
    // ★ 对"到位"的定义是"误差**连续** stay 在容差内 hold_s 秒"，
    //   而不是"某一拍碰巧进了容差"。写控制任务时这个区别是本质的：
    //   前者才是"稳定"，后者可能只是一个过零点。
    if (std::abs(error) < goal->tolerance) {
      goal_hold_accum_s_ += dt.seconds();
    } else {
      goal_hold_accum_s_ = 0.0;   // 一旦出容差，重新计时
    }

    if (goal_hold_accum_s_ >= goal->hold_s) {
      result->success = true;
      result->message =
        "已在 ±" + std::to_string(goal->tolerance) + " 内保持 " +
        std::to_string(goal->hold_s) + "s";
      goal_handle_->succeed(result);
      RCLCPP_INFO(
        get_logger(), "calibrate 成功: 到达 %.3f, 误差 %.4f, 耗时 %.2fs",
        current_value, error, elapsed);
      clearGoal();
      return;
    }

    // ---- 结局 3：超时，失败 ----
    // ★ 任务必须有超时。没有超时的 action 等于给客户端埋了一个永久悬挂的坑：
    //   目标永远达不到，goal 就永远是"执行中"，客户端只能干等或取消。
    //   abort() 和 succeed() 的区别就是"失败"和"成功"，客户端能分辨。
    if (elapsed >= goal->timeout_s) {
      result->success = false;
      result->message =
        "超时 " + std::to_string(goal->timeout_s) + "s 仍未收敛到容差内";
      goal_handle_->abort(result);
      RCLCPP_ERROR(
        get_logger(), "calibrate 超时: 当前 %.3f, 误差 %.4f", current_value, error);
      clearGoal();
      return;
    }

    // ---- 还没结束：按节流发一次进度 ----
    if ((stamp - last_feedback_time_).seconds() < feedback_interval_s_) {
      return;
    }
    last_feedback_time_ = stamp;

    auto feedback = std::make_shared<Calibrate::Feedback>();
    feedback->current_value = current_value;
    feedback->current_error = error;
    feedback->elapsed_s = elapsed;
    // 进度 = 从初始误差收敛掉了多少。初始误差为 0 时直接算 100%。
    feedback->progress = (std::abs(goal_initial_error_) > 1e-9)
      ? std::clamp(1.0 - std::abs(error) / std::abs(goal_initial_error_), 0.0, 1.0)
      : 1.0;
    goal_handle_->publish_feedback(feedback);
  }

  /// 任务结束（无论什么结局）后一定要清干净，否则下一个 goal 会被 REJECT 挡在门外
  void clearGoal()
  {
    goal_handle_.reset();
    goal_hold_accum_s_ = 0.0;
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

  // ---------------------------------------------------------------------------
  //  action 的状态
  //
  //  ★ 这组成员就是"action 服务端到底比 service 多管了什么"的答案。
  //    service 是纯函数式的：进来一个请求，返回一个响应，不留痕迹。
  //    action 则必须**把任务本身存成状态**：谁在执行、什么时候开始的、
  //    已经稳住多久了、上次汇报进度是什么时候。这些全得你自己维护。
  //
  //  ★ 非空 = 有任务在执行。所以它同时兼任"当前任务"和"忙闲标志位"，
  //    上面 handleGoal 拒绝第二个目标时查的就是它。
  // ---------------------------------------------------------------------------
  rclcpp_action::Server<Calibrate>::SharedPtr action_server_;
  std::shared_ptr<GoalHandleCalibrate> goal_handle_;
  rclcpp::Time goal_start_time_;
  rclcpp::Time last_feedback_time_;
  double goal_initial_error_{0.0};   ///< 任务开始时的误差，算 progress 的基准
  double goal_hold_accum_s_{0.0};    ///< 已在容差内连续保持了多少秒
  double feedback_interval_s_{0.1};  ///< 反馈节流间隔
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ControllerNode>());
  rclcpp::shutdown();
  return 0;
}
