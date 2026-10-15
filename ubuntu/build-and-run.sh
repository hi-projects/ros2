#!/usr/bin/env bash
# =============================================================================
#  在 Ubuntu **宿主机上**执行：编译 + 跑一遍所有能跑的东西。
#
#  用法（在仓库根目录，或者任意位置都行）：
#
#      bash ubuntu/build-and-run.sh
#
#  这是 docker/build-and-run.sh 的"裸机版"，做的事一模一样。
#  区别只有一处，但那处很关键：**路径不再写死成 /ws**。
#
#  ---------------------------------------------------------------------------
#  为什么容器版要写死 /ws，裸机版不能？
#
#      Docker 版：run.ps1 把仓库挂到容器的 /ws，所以容器里路径**永远是** /ws。
#                 写死反而省事。
#      裸机版  ：仓库在哪儿取决于你 clone 到哪儿了
#                 （本机是 /home/byd/workspace/ros2-master）。
#                 而且这个脚本会被从各种位置调用 ——
#                   bash ubuntu/build-and-run.sh
#                   cd ubuntu && bash build-and-run.sh
#                   /home/byd/workspace/ros2-master/ubuntu/build-and-run.sh
#                 所以必须自己算出仓库根目录，不能让用户先 cd 过去。
#
#  ★ 算根目录这行是 Bash 里少数几个"必须这么写"的固定套路之一：
#
#      SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
#
#    - ${BASH_SOURCE[0]}          脚本自己的路径（可能是相对的：ubuntu/xxx.sh）
#    - dirname                    取所在目录（ubuntu）
#    - cd ... && pwd              进那个目录再问"现在在哪" -> 拿到**绝对**路径
#    - 最外面再 cd 一层            从 ubuntu/ 回到仓库根
#
#    为什么不直接用 $0 或 pwd？
#      $0        在被 source 的时候会变成 shell 的名字，不可靠
#      pwd       只是"你现在在哪"，和脚本在哪没有任何关系
#    -p 是为了处理软链接（比如你把脚本 ln 到 /usr/local/bin 去了）。
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "$WS"

DISTRO="${ROS_DISTRO:-humble}"

# -----------------------------------------------------------------------------
# 输出小工具
# -----------------------------------------------------------------------------
step() {
  echo
  echo "=============================================================="
  echo " $*"
  echo "=============================================================="
}
c_warn() { printf '\033[33m%s\033[0m\n' "$*"; }
c_ok()   { printf '\033[32m%s\033[0m\n' "$*"; }

# -----------------------------------------------------------------------------
# ★★ 一个非常具体的坑：`set -u` 和 ROS2 的 setup.bash 不兼容
#
#   本脚本开了 `set -euo pipefail`，其中 -u (nounset) 表示
#   "用了没定义过的变量就报错退出"。这是个好习惯，但它和 ROS2 撞车：
#
#       /opt/ros/humble/setup.bash: line 8: AMENT_TRACE_SETUP_FILES: unbound variable
#
#   ROS2 的 setup.bash 内部会读一堆**可能没定义**的变量
#   （AMENT_TRACE_SETUP_FILES、COLCON_PREFIX_PATH……），
#   判断"有没有"用的就是"读一下看看"，这在 -u 下等于直接自杀。
#
#   ★ 这不是 bug，是两套约定打架：
#       脚本作者的约定：变量没定义就是错误      -> 开 -u
#       环境脚本的约定：变量没定义是正常状态    -> 靠 $VAR 探测
#
#   解法：source 前后临时关掉 -u。**只关这一小段**，不要图省事把整个脚本的 -u 去掉 ——
#   那样就少了一个帮你抓拼写错误（$WS 写成 $SW）的工具。
#
#   （docker/build-and-run.sh 没这个问题，因为它只开了 -e，没开 -u。
#     裸机版保留 -u 是有意的，顺便把这个坑记在这里。）
# -----------------------------------------------------------------------------
source_ros() {
  set +u
  # shellcheck disable=SC1090,SC1091
  source "$1"
  set -u
}

if [ ! -f "/opt/ros/${DISTRO}/setup.bash" ]; then
  c_warn "找不到 /opt/ros/${DISTRO}/setup.bash —— 这台机器上可能还没装 ROS2。"
  c_warn "先跑一次安装脚本："
  echo  "    bash ubuntu/setup-ros2.sh"
  exit 1
fi
source_ros "/opt/ros/${DISTRO}/setup.bash"

step "0. 环境"
echo "  仓库根目录 : $WS"
echo "  ROS 发行版 : ${ROS_DISTRO:-?}  ($(command -v ros2))"
echo "  colcon     : $(command -v colcon)"

if ! command -v colcon >/dev/null 2>&1; then
  c_warn "  colcon 不在 PATH 里。装一下："
  echo  "    bash ubuntu/setup-ros2.sh"
  exit 1
fi

# 上一轮如果还在跑，先清掉。★ 这一步在裸机上比在容器里更重要：
#   容器用完就没了，宿主机上的残留进程会一直活着，
#   下次再跑时"上一个测试的节点"还在发数据，排查起来非常费劲
#   （本工程开发时就踩过一次，见 README 里 /reset vs /controller/reset 那段）。
for n in controller_node monitor_node tune_client_node; do
  pkill -x "$n" 2>/dev/null || true
done
sleep 0.5

echo
echo "=============================================================="
echo " 1. colcon build"
echo "=============================================================="
# 不加 --symlink-install。原因见 docker/run.ps1 里的说明：
# 那会让 install/ 变成指向 src/ 的软链，掩盖"源码目录 ≠ 运行时目录"这个事实。
colcon build

echo
echo "=============================================================="
echo " 2. source 安装结果"
echo "=============================================================="
# ★ 这一步是新手最容易漏的。
#   colcon build 只是把东西装到 install/ 下，
#   要让当前 shell 能找到这些包（ros2 run / find_package），必须 source。
#   每个新开的终端都要 source 一次。
#
#   注意它和 /opt/ros/humble/setup.bash 是**两件事**：
#     前者让 shell 认识 ROS2 本身
#     后者让 shell 认识**你这个工作空间里**的包
#   两个都要 source，而且顺序不能反（工作空间的会去扩展 ROS2 的环境）。
source_ros "install/setup.bash"
echo "AMENT_PREFIX_PATH 前几项："
echo "$AMENT_PREFIX_PATH" | tr ':' '\n' | head -6

echo
echo "=============================================================="
echo " 3. 跑离线工具 tune_cli（不是 ROS 节点，不需要起 DDS）"
echo "=============================================================="
# ★ 容器版这里有一句 chmod +x，因为 Windows 文件系统挂进容器时会丢权限位。
#   裸机上不需要 —— 这就是"换到 Ubuntu 反而少一个坑"的例子。
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
echo "--- 高层视角对照（同一个东西，两种看法）---"
ros2 action list -t

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
echo "  cd $WS"
echo "  source /opt/ros/${DISTRO}/setup.bash && source install/setup.bash   # 新终端每次都要"
echo
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
