#!/usr/bin/env bash
# =============================================================================
#  在容器**内部**执行：编译 + 跑一遍所有能跑的东西。
#
#  用法（容器里）：
#      bash /ws/docker/build-and-run.sh
#
#  这个脚本本身就是一份"最小使用说明"，把它当 cheatsheet 看。
# =============================================================================
set -e

WS=/ws
cd "$WS"

source /opt/ros/humble/setup.bash

echo "=============================================================="
echo " 1. colcon build"
echo "=============================================================="
# 不加 --symlink-install，见 docker/run.ps1 里的说明
colcon build

echo
echo "=============================================================="
echo " 2. source 安装结果"
echo "=============================================================="
# ★ 这一步是新手最容易漏的。
#   colcon build 只是把东西装到 install/ 下，
#   要让当前 shell 能找到这些包（ros2 run / find_package），必须 source。
#   每个新开的终端都要 source 一次。
source install/setup.bash
echo "AMENT_PREFIX_PATH 前几项："
echo "$AMENT_PREFIX_PATH" | tr ':' '\n' | head -6

echo
echo "=============================================================="
echo " 3. 跑离线工具 tune_cli（不是 ROS 节点，不需要起 DDS）"
echo "=============================================================="
# 先给可执行权限（Windows 文件系统挂进容器时经常丢权限位）
chmod +x install/demo_tools/lib/demo_tools/tune_cli 2>/dev/null || true
./install/demo_tools/lib/demo_tools/tune_cli

echo
echo "=============================================================="
echo " 4. 确认 ROS2 能看见我们的三种自定义接口"
echo "=============================================================="
ros2 interface show demo_interfaces/msg/Target
echo "---"
ros2 interface show demo_interfaces/srv/Reset
echo "---"
ros2 interface show demo_interfaces/action/Calibrate

echo
echo "=============================================================="
echo " 5. 跑节点 + 发一次 action 校准任务"
echo "=============================================================="
# 两个常驻节点放后台。校准任务本身要跑十几秒，所以 timeout 给得比原来宽松。
timeout 90 ros2 run demo_nodes monitor_node --ros-args -p print_every:=150 > /tmp/monitor.log 2>&1 &
MONITOR_PID=$!
timeout 90 ros2 run demo_nodes controller_node > /tmp/controller.log 2>&1 &
CONTROLLER_PID=$!
sleep 3

echo "--- 还没发目标时，和 /calibrate 有关的实体 ---"
echo "服务（含隐藏名）："
ros2 service list --include-hidden-services | grep calibrate || true
echo "话题（含隐藏名）："
ros2 topic list --include-hidden-topics | grep calibrate || true
echo "   ★ 3 个服务 + 2 个话题 = 一个 action 的全部。"
echo "     没有隐藏的第四种通信原语 —— action 在网络上不存在。"
echo "     这五个实体在 controller_node 一启动就全建好了，和有没有任务无关。"

echo
echo "--- 发一个校准目标，看反馈和结果 ---"
# 客户端会自己退出，退出码有含义：0=成功 1=目标被拒绝 2=失败或被取消
CLIENT_RC=0
ros2 run demo_nodes tune_client_node || CLIENT_RC=$?
echo "   tune_client_node 退出码 = $CLIENT_RC  (0=成功 1=被拒绝 2=失败/取消)"

echo
echo "--- 服务端那边看到的日志 ---"
grep -E "calibrate|reset" /tmp/controller.log | tail -6 || true

# ★ 收尾要杀**两层**：
#   $! 拿到的是 timeout 的 pid，timeout 下面是 ros2 run（python 启动器），
#   再下面才是真正的节点可执行文件。只 kill $! 常常留下孤儿节点，
#   下次再跑脚本时"上一个测试的节点"还在发数据 —— 排查起来非常费劲。
kill $CONTROLLER_PID $MONITOR_PID 2>/dev/null || true
pkill -x controller_node 2>/dev/null || true
pkill -x monitor_node 2>/dev/null || true
sleep 1

echo
echo "全部跑完。接下来可以自己试："
echo "  source install/setup.bash"
echo "  ros2 launch demo_nodes demo.launch.py                         # 两个常驻节点"
echo "  ros2 launch demo_nodes demo.launch.py with_client:=true       # 顺便发一次校准任务"
echo "  ros2 run demo_tools tune_cli          # 注意是 demo_tools，不是 demo_nodes"
echo "  ros2 param list /controller"
echo "  ros2 service call /reset demo_interfaces/srv/Reset \"{new_setpoint: 2.0}\""
echo
echo "  # ★ 注意上面是 /reset，不是 /controller/reset ——"
echo "  #   相对名拼的是**命名空间**，不是节点名。详见 controller_node.cpp 里的注释。"
echo
echo "  # 用命令行直接发 action 目标（不用写客户端）"
echo "  ros2 action send_goal /calibrate demo_interfaces/action/Calibrate \\"
echo "      \"{target: 1.0, tolerance: 0.1, hold_s: 0.5, timeout_s: 25.0}\" --feedback"
echo
echo "  # 看『取消』长什么样：任务跑到第 2 秒就被叫停"
echo "  ros2 run demo_nodes tune_client_node --ros-args \\"
echo "      -p target:=0.5 -p cancel_after_s:=2.0"
