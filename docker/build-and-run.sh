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
echo " 4. 确认 ROS2 能看见我们的自定义消息"
echo "=============================================================="
ros2 interface show demo_interfaces/msg/Target
echo "---"
ros2 interface show demo_interfaces/srv/Reset

echo
echo "=============================================================="
echo " 5. 跑节点（10 秒后自动结束）"
echo "=============================================================="
# 后台起监视器，等它输出完再停
timeout 10 ros2 run demo_nodes monitor_node --ros-args -p print_every:=5 &
MONITOR_PID=$!
sleep 1
timeout 9 ros2 run demo_nodes controller_node &
CONTROLLER_PID=$!
wait $CONTROLLER_PID 2>/dev/null || true
wait $MONITOR_PID 2>/dev/null || true

echo
echo "全部跑完。接下来可以自己试："
echo "  source install/setup.bash"
echo "  ros2 launch demo_nodes demo.launch.py"
echo "  ros2 run demo_tools tune_cli          # 注意是 demo_tools，不是 demo_nodes"
echo "  ros2 param list /controller"
echo "  ros2 service call /controller/reset demo_interfaces/srv/Reset \"{new_setpoint: 2.0}\""
