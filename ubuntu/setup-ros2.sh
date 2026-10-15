#!/usr/bin/env bash
# =============================================================================
#  在 Ubuntu 上把"跑这个工程需要的东西"装齐。
#
#  用法（在仓库根目录，或者任意位置都行）：
#
#      bash ubuntu/setup-ros2.sh                # 检测 + 缺什么装什么
#      bash ubuntu/setup-ros2.sh --check-only   # 只检测、只打印，不动系统
#      ROS_MIRROR=tuna bash ubuntu/setup-ros2.sh
#      ROS_DISTRO=humble bash ubuntu/setup-ros2.sh
#
#  这个脚本是**幂等**的：已经装好的不会重复装，可以反复跑。
#  它做的每一件事都是"先检测、再决定"，所以第一次跑和第五次跑都安全。
#
#  ---------------------------------------------------------------------------
#  为什么要有这个脚本？
#
#  本工程原本只有 Docker 一条路（docker/run.ps1，Windows 上用的）。
#  换到 Ubuntu 之后，容器那一层其实是多余的 —— ROS2 本来就装在系统里。
#  但"装齐依赖"这件事在裸机上反而更麻烦，因为：
#
#    - ROS2 不在 Ubuntu 官方源里，得先加它自己的 apt 源和 GPG key
#    - Humble 只支持 jammy (22.04)，版本对不上会装出一堆奇怪问题
#    - 编译自定义接口要 rosidl_default_generators，这个包很多人不知道要装
#    - action 要 rclcpp_action，它**不在** rclcpp 里，是单独一个包
#
#  所以这个脚本真正的作用是：把"该装哪几个包"这件知识固化下来。
#  每一个包后面都写了"为什么需要它"，可以直接当清单读。
# =============================================================================

set -euo pipefail

# -----------------------------------------------------------------------------
# 0. 默认值
# -----------------------------------------------------------------------------
DISTRO="${ROS_DISTRO:-humble}"

# 镜像源。国内直连 packages.ros.org 很慢，默认走中科大（本工程实测可用）。
#   ustc     中科大        https://mirrors.ustc.edu.cn/ros2/ubuntu
#   tuna     清华          https://mirrors.tuna.tsinghua.edu.cn/ros2/ubuntu
#   official 官方          http://packages.ros.org/ros2/ubuntu
MIRROR="${ROS_MIRROR:-ustc}"

CHECK_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --check-only) CHECK_ONLY=1 ;;
    -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "未知参数：$arg（用 --help 看用法）" >&2; exit 2 ;;
  esac
done

# -----------------------------------------------------------------------------
# 输出小工具。★ 和 ★★ 是"值得看一眼"和"必须看一眼"，和 README 里一致。
# -----------------------------------------------------------------------------
c_ok()   { printf '\033[32m%s\033[0m\n' "$*"; }
c_warn() { printf '\033[33m%s\033[0m\n' "$*"; }
c_err()  { printf '\033[31m%s\033[0m\n' "$*" >&2; }
c_dim()  { printf '\033[2m%s\033[0m\n' "$*"; }

step() { echo; echo "=============================================================="; echo " $*"; echo "=============================================================="; }

# sudo：脚本本身不要求 root，需要的时候自己加。
# 已经是 root（比如在容器里）就不加，免得报 "sudo: command not found"。
if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
else
  SUDO="sudo"
fi

# =============================================================================
# 1. 检查操作系统：Humble 只支持 Ubuntu 22.04 (jammy)
#
#    ★ 这一条必须先查，而且要查死。ROS2 的每个发行版和 Ubuntu 版本是**绑死**的：
#        Humble  -> jammy (22.04)
#        Iron    -> jammy
#        Jazzy   -> noble (24.04)
#      在 24.04 上装 humble 的包，apt 会告诉你"找不到候选版本"，
#      而这个报错完全看不出是版本问题。不如在这里先说清楚。
# =============================================================================
step "1. 检查系统"

if [ ! -f /etc/os-release ]; then
  c_err "找不到 /etc/os-release，这看起来不是标准的 Linux 发行版。"
  exit 1
fi
# shellcheck disable=SC1091
. /etc/os-release

echo "  发行版   : ${PRETTY_NAME:-未知}"
echo "  架构     : $(dpkg --print-architecture)"
echo "  目标 ROS : ${DISTRO}"

if [ "${ID:-}" != "ubuntu" ]; then
  c_warn "  ⚠ 这不是 Ubuntu（是 ${ID:-未知}）。ROS2 官方只为 Ubuntu 提供二进制包，"
  c_warn "    下面的 apt 步骤大概率不适用。"
fi

# ★ ROS2 的发行版和 Ubuntu 版本是**绑死**的，而且绑法不直观：
#     foxy   -> focal   (20.04)   已 EOL
#     humble -> jammy   (22.04)   ★ 本工程用的
#     iron   -> jammy   (22.04)
#     jazzy  -> noble   (24.04)
#   注意 humble 和 iron 共用 jammy，所以"版本对得上"并不唯一。
#   与其让 apt 在最后报一句"无法定位软件包"（完全看不出是版本问题），
#   不如现在就说清楚。
declare -A CODENAME_OF=(
  [foxy]=focal [humble]=jammy [iron]=jammy [jazzy]=noble [rolling]=noble
)

if [ -n "${CODENAME_OF[$DISTRO]:-}" ]; then
  want="${CODENAME_OF[$DISTRO]}"
  have="${VERSION_CODENAME:-未知}"
  if [ "$have" != "$want" ]; then
    c_warn "  ⚠ ${DISTRO} 对应的是 Ubuntu ${want}，而当前是 ${have}。"
    c_warn "    apt 多半会报『无法定位软件包 ros-${DISTRO}-...』—— 那不是脚本坏了，是版本不匹配。"
    c_warn "    对照表：foxy=20.04 / humble,iron=22.04 / jazzy=24.04"
  fi
else
  c_warn "  ⚠ 不认识的 ROS_DISTRO=${DISTRO}，无法判断它该配哪个 Ubuntu 版本。"
  c_warn "    （已知：foxy / humble / iron / jazzy / rolling）"
fi

# =============================================================================
# 2. 检查 apt 源
#
#    ROS2 的包不在 Ubuntu 官方源里。要先用两个东西把它接进来：
#      ① GPG key  —— 让 apt 能验证"这个包确实是 ROS 官方签的"
#      ② sources.list 里的一行 deb —— 告诉 apt 去哪里找
#
#    ★ 判断"源到底加没加"的正确方式不是看某个固定文件名。
#      别人可能叫 ros2.list、ros-fish.list、ros.list……名字随便起。
#      真正该问的是：**apt 认不认这个源**。
#      所以下面用 grep 扫 sources.list 和 sources.list.d/* 的**内容**，
#      而不是判断文件在不在。
# =============================================================================
step "2. 检查 ROS2 apt 源"

APT_LIST_DIR="/etc/apt/sources.list.d"
APT_LIST_FILE="${APT_LIST_DIR}/ros2-${DISTRO}.list"

# 扫所有源的**内容**，找有没有指向 ros2/ubuntu 的行
existing_src="$(grep -rhs "ros2/ubuntu\|packages.ros.org/ros2" \
                     /etc/apt/sources.list "$APT_LIST_DIR" 2>/dev/null | head -1 || true)"

have_key=0
if ls /etc/apt/trusted.gpg.d/ 2>/dev/null | grep -qi "ros"; then
  have_key=1
elif [ -f /usr/share/keyrings/ros-archive-keyring.gpg ]; then
  have_key=1
fi

if [ -n "$existing_src" ]; then
  c_ok "  已有 ROS2 源："
  c_dim "    $existing_src"
else
  c_warn "  还没有 ROS2 apt 源，需要添加。"
fi

if [ "$have_key" -eq 1 ]; then
  c_ok "  ROS 的 GPG key 已就位。"
else
  c_warn "  缺 ROS 的 GPG key，需要下载。"
fi

# =============================================================================
# 3. 检查编译依赖
#
#    ★ 这一节是全文最该看的部分：**每个包后面都写了"为什么"**。
#      想只装最小的集合时，照着这个清单挑就行。
#
#    ★★ 特别注意 rosidl-default-generators 和 rclcpp-action 这两个：
#        它们都不在 rclcpp 里，而且不知道要装的话，报错信息很难懂 ——
#        前者报"找不到 rosidl_generate_interfaces 宏"，
#        后者报"fatal error: rclcpp_action/rclcpp_action.hpp: No such file"。
# =============================================================================
step "3. 检查编译依赖"

# 格式： "包名|为什么要它"
#
# ★ 这里列的是**编译会失败的**那几个。故意不写 ros-base 那种大礼包 ——
#   因为"能不能编译"这件事，取决于下面这几个具体的能力，
#   而不是取决于装的是哪个元包。
PKGS=(
  "ros-${DISTRO}-ament-cmake|ament 的 CMake 宏（ament_target_dependencies 等）"
  "ros-${DISTRO}-rosidl-default-generators|把 .msg/.srv/.action 生成 C++ 代码（没这个连自研接口都编不了）"
  "ros-${DISTRO}-rclcpp|写 ROS 节点"
  "ros-${DISTRO}-rclcpp-action|写 action server/client（★ 它不在 rclcpp 里，是独立包）"
  "ros-${DISTRO}-std-msgs|Target.msg 里嵌了 std_msgs/Header"
  "ros-${DISTRO}-action-msgs|action 生成的类型里嵌了 action_msgs 的 CancelGoal / GoalInfo"
  "build-essential|g++ / make"
  "cmake|就是它（工程名字里有它）"
)

missing=()
present=()

for entry in "${PKGS[@]}"; do
  pkg="${entry%%|*}"
  why="${entry#*|}"
  if dpkg -l "$pkg" 2>/dev/null | grep -q '^ii'; then
    present+=("$pkg")
    printf '  \033[32m✓\033[0m %-42s \033[2m%s\033[0m\n' "$pkg" "$why"
  else
    missing+=("$pkg")
    printf '  \033[31m✗\033[0m %-42s \033[2m%s\033[0m\n' "$pkg" "$why"
  fi
done

ros_present=0
for p in "${present[@]}"; do
  case "$p" in ros-*) ros_present=$((ros_present + 1)) ;; esac
done

# -----------------------------------------------------------------------------
# 命令级的检查
#
# ★★ 这里有个**必须踩过才懂**的点：**判断"有没有"要问命令，不要问包管理器。**
#
#    colcon 就是最好的例子：apt 装的话包名是 python3-colcon-common-extensions，
#    pip 装的话压根没有这个包。这台开发机就是 pip 装的 ——
#    dpkg 查不到它，但 colcon 命令明明能用。
#    只信 dpkg 的话，脚本会劝一个已经能用的机器去装一遍 colcon。
#
#    ros2 的各个动词（run / action / topic / interface）也是同理：
#    它们的包名在不同安装方式下并不统一，但"命令能不能跑"是统一的。
# -----------------------------------------------------------------------------
echo
c_dim "  --- 命令检查（比上面的包检查更接近真相）---"

colcon_ok=0
if command -v colcon >/dev/null 2>&1; then
  colcon_ok=1
  c_ok "  colcon 可用：$(command -v colcon)"
  # 上面 PKGS 里没列 colcon，所以这里不该产生 missing 项。
else
  c_warn "  colcon 不可用（下面会装上）"
fi

ros_setup="/opt/ros/${DISTRO}/setup.bash"
if [ -f "$ros_setup" ]; then
  c_ok "  $ros_setup 存在"

  # ★ ros2 是 source 之后才有的命令。所以要在一个**子 shell** 里 source 再查，
  #   否则查的是当前 shell 的环境 —— 而当前 shell 很可能没 source 过，
  #   于是明明装好了也报"不可用"，白白吓人一跳。
  verbs_ok="$(bash -c "source '$ros_setup' >/dev/null 2>&1 && \
      for v in run action topic service interface param node launch; do \
        ros2 \$v --help >/dev/null 2>&1 || echo -n \"\$v \"; \
      done" 2>/dev/null || true)"

  if [ -z "$verbs_ok" ]; then
    c_ok "  ros2 的 8 个子命令全部可用（run/action/topic/service/interface/param/node/launch）"
  else
    c_warn "  ros2 这些子命令不可用：$verbs_ok"
    c_warn "    （这套 CLI 在 ros-${DISTRO}-ros-base 里，装上就有了）"
  fi
else
  c_warn "  $ros_setup 不存在 —— 这台机器上还没装 ROS2"
fi

# =============================================================================
# 4. 汇总"到底还要装什么"
#
#    ★ 两种情形，对应两种装法：
#
#      情形 A：这台机器根本没装 ROS2（ros_setup 不存在）
#              -> 装 ros-${DISTRO}-ros-base 一个大礼包，一次到位。
#                 它是官方元包，把 rclcpp / ros2 CLI / 常用消息全带上了。
#                 比一个个挑包省事得多。
#
#      情形 B：装过 ROS2，只是缺了几个（比如 action 相关的）
#              -> 只补缺的那几个。装大礼包反而会拖进来一堆用不上的东西。
#
#    判断依据用 ros_setup 在不在，而不是数包个数 —— 那个文件是 ROS2
#    "装好了"的标志（它就是 /opt/ros/<distro>/ 环境的入口）。
# =============================================================================
to_install=("${missing[@]}")

if [ ! -f "/opt/ros/${DISTRO}/setup.bash" ]; then
  echo
  c_warn "  这台机器上还没有 ROS2，改用一个元包装（比逐个挑包省事）："
  # ★ ros-base 覆盖了 rclcpp / ros2 CLI / 常用消息，但**不含**下面这两个 ——
  #   所以哪怕走"大礼包"路线，这两个也得单独点名：
  #     rosidl-default-generators  生成 .msg/.srv/.action 的宏，不在 ros-base 里
  #     rclcpp-action              写 action 节点的库，不在 rclcpp 里
  to_install=(
    "ros-${DISTRO}-ros-base"
    "ros-${DISTRO}-rosidl-default-generators"
    "ros-${DISTRO}-rclcpp-action"
  )
  c_dim "    ros-${DISTRO}-ros-base 已包含 rclcpp / ros2 CLI / 常用消息"
  c_dim "    另外单独补两个它不含的：rosidl-default-generators、rclcpp-action"
fi

if [ "$colcon_ok" -eq 0 ]; then
  to_install+=("python3-colcon-common-extensions")
fi

# 去掉重复项（数组里可能有重复，apt 不介意，但打印出来难看）
if [ "${#to_install[@]}" -gt 0 ]; then
  mapfile -t to_install < <(printf '%s\n' "${to_install[@]}" | awk '!seen[$0]++')
fi

# =============================================================================
# 5. --check-only：只报告，不动系统
# =============================================================================
if [ "$CHECK_ONLY" -eq 1 ]; then
  step "检测完毕（--check-only，没有改动系统）"

  need_src=0
  { [ -z "$existing_src" ] || [ "$have_key" -eq 0 ]; } && need_src=1

  if [ "${#to_install[@]}" -eq 0 ] && [ "$need_src" -eq 0 ]; then
    c_ok "  一切都齐了，直接跑就行："
    echo "      bash ubuntu/build-and-run.sh"
  else
    if [ "$need_src" -eq 1 ]; then
      c_warn "  还差：ROS2 的 apt 源 / GPG key"
    fi
    if [ "${#to_install[@]}" -gt 0 ]; then
      c_warn "  还差 ${#to_install[@]} 个包：${to_install[*]}"
    fi
    echo
    echo "  去掉 --check-only 跑一次，脚本会把它们装上："
    echo "      bash ubuntu/setup-ros2.sh"
    echo
    echo "  想自己手动装（等价于脚本会做的事）："
    [ "$need_src" -eq 1 ] && echo "      # 先按 §6 添加 ROS2 apt 源和 GPG key"
    echo "      $SUDO apt-get update"
    echo "      $SUDO apt-get install -y ${to_install[*]:-<无>}"
  fi
  exit 0
fi

# =============================================================================
# 6. 真正开始装：先把 apt 源接好
# =============================================================================
if [ -z "$existing_src" ] || [ "$have_key" -eq 0 ]; then

  step "6. 添加 ROS2 apt 源"

  if [ "$have_key" -eq 0 ]; then
    echo "  下载 GPG key ..."
    # ★ 两个来源互相备份：国内访问 GitHub 常常不通，
    #   而 packages.ros.org 在配好源之前就应该能访问（否则装了也没用）。
    key_urls=(
      "https://raw.githubusercontent.com/ros/rosdistro/master/ros.key"
      "http://packages.ros.org/ros.key"
    )
    got_key=0
    for url in "${key_urls[@]}"; do
      if $SUDO curl -fsSL "$url" -o /tmp/ros-archive-keyring.gpg 2>/dev/null \
         && [ -s /tmp/ros-archive-keyring.gpg ]; then
        $SUDO cp /tmp/ros-archive-keyring.gpg /usr/share/keyrings/ros-archive-keyring.gpg
        c_ok "  已从 $url 取得 key"
        have_key=1
        got_key=1
        break
      fi
      c_dim "  （$url 不通，换下一个）"
    done

    if [ "$got_key" -eq 0 ]; then
      c_err "  两个来源都拿不到 ROS 的 GPG key。"
      c_err "  手动解决：下载 ros.key 后放到 /usr/share/keyrings/ros-archive-keyring.gpg"
      exit 1
    fi
  fi

  if [ -z "$existing_src" ]; then
    case "$MIRROR" in
      ustc)     repo_url="https://mirrors.ustc.edu.cn/ros2/ubuntu" ;;
      tuna)     repo_url="https://mirrors.tuna.tsinghua.edu.cn/ros2/ubuntu" ;;
      official) repo_url="http://packages.ros.org/ros2/ubuntu" ;;
      *)        c_err "  未知的 ROS_MIRROR=$MIRROR（可选 ustc / tuna / official）"; exit 2 ;;
    esac

    codename="${VERSION_CODENAME:-jammy}"
    arch="$(dpkg --print-architecture)"

    echo "  写入 $APT_LIST_FILE"
    echo "  镜像：$repo_url"
    echo
    # ★ 这一行的三个部分各有作用，值得看清楚：
    #     arch=          只装本机架构的包（aarch64 机器上写 arm64）
    #     signed-by=     指定用哪个 key 验证 —— 比全局 trusted.gpg.d 更干净
    #     <url> <codename> main   在哪儿找、找哪个版本、找哪个组件
    echo "deb [arch=${arch} signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] ${repo_url} ${codename} main" \
      | $SUDO tee "$APT_LIST_FILE" >/dev/null
    c_ok "  源已写入。"
  fi
fi

step "7. apt-get update"
$SUDO apt-get update

if [ "${#to_install[@]}" -gt 0 ]; then
  step "8. 安装缺失的包（${#to_install[@]} 个）"
  echo "  ${to_install[*]}"
  echo
  $SUDO apt-get install -y "${to_install[@]}"
  c_ok "  安装完成。"
else
  step "8. 没有缺失的包，跳过安装"
fi

# -----------------------------------------------------------------------------
# 9. rosdep：可选，但推荐
#
#    ★ 它和编译无关，不装也能 build。它管的是另一件事：
#      "package.xml 里声明的依赖，在真实系统上到底装没装"。
#      编译能过 ≠ 依赖声明写全了（本机 /opt/ros 里恰好什么都有，
#      会把漏声明全部掩盖掉）。详见 README §7.4。
# -----------------------------------------------------------------------------
step "9. rosdep（可选）"

if command -v rosdep >/dev/null 2>&1; then
  c_ok "  rosdep 已装：$(command -v rosdep)"
  if [ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]; then
    echo "  还没有初始化过，执行 rosdep init ..."
    $SUDO rosdep init 2>/dev/null || c_warn "  rosdep init 失败（可能已初始化过，可忽略）"
  fi
  c_dim "  （第一次用要跑：rosdep update —— 它下载依赖规则库，比较慢，只跑一次）"
else
  if dpkg -l python3-rosdep 2>/dev/null | grep -q '^ii'; then
    c_warn "  python3-rosdep 装了但没有 rosdep 命令 —— 试试重新登录或用 /usr/bin/rosdep"
  else
    c_warn "  没装 rosdep。不影响编译，但建议装："
    echo "      $SUDO apt-get install -y python3-rosdep"
    echo "      $SUDO rosdep init && rosdep update"
  fi
fi

# =============================================================================
# 10. 收尾
# =============================================================================
step "10. 完成"

echo "  已验证："
[ -f "/opt/ros/${DISTRO}/setup.bash" ] \
  && c_ok "    /opt/ros/${DISTRO}/setup.bash 存在" \
  || c_warn "    /opt/ros/${DISTRO}/setup.bash 不在 —— 包可能没装成功"

# ★ 注意这里没有替用户 source。原因：
#   这个脚本是个子进程，它 source 出来的环境**不会**影响调用它的 shell。
#   想让它生效必须 `source ubuntu/setup-ros2.sh`，但那样脚本里的 exit
#   会把用户的 terminal 一起关掉 —— 得不偿失。所以干脆让用户自己敲一行。
echo
echo "  ★ 每个新开的终端都要先 source 一次（这一步新手最容易漏）："
echo "      source /opt/ros/${DISTRO}/setup.bash"
echo
echo "  然后就能跑本工程了："
echo "      bash ubuntu/build-and-run.sh"
