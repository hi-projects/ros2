# ROS2 × CMake 实战教程

一个能直接跑的 ROS2 Humble 工程，专门用来把 **ROS2 与 CMake 的关系**讲透。

不是"照着敲一遍能跑"的 demo —— 每个 `CMakeLists.txt` 都当教材写，
逐行注释了"这条命令在干什么、为什么必须这么写、写错了会报什么错"。

---

## 0. 这个例子是什么

一个 PID 闭环控制 demo。控制器的参数由另一个包里的库提供，
接口类型由第三个包生成，还有一个纯命令行工具用来离线整定参数。

```
                    demo_interfaces          自定义 msg / srv / action
                    （代码生成）              ．不产生任何可执行文件
                          │                   ．只产生 C++ 头文件
                          │                   ．学: rosidl_generate_interfaces
                          ▼
    demo_math ────────► 纯计算库                ．add_library + 公开头文件
    （库包）              PID 控制器             ．学: ament_export_targets
                          │                      ament_export_dependencies
                          │
                          ▼
    demo_tools ───────► 库 + 命令行工具          ．自己又有库，又用别人的库
    （库 + 工具）         AutoTuner + tune_cli    ．学: PUBLIC / PRIVATE 传递性
                          │                      ．学: 纯标准 CMake 写法
                          │
                          ▼
    demo_nodes ───────► 三个 ROS 节点            ．只产生可执行文件
    （节点包）            controller             ．学: install 可执行文件与资源
                          monitor
                          tune_client
```

四个包刚好覆盖四种典型情况：

| 包 | 产出 | 学到的 CMake 机制 |
|---|---|---|
| `demo_interfaces` | 头文件（由 `.msg`/`.srv`/`.action` 生成） | 代码生成如何挂到 CMake 上 |
| `demo_math` | 共享库 `libdemo_math.so` | 怎么把库**导出**给别人用 |
| `demo_tools` | 共享库 + 可执行文件 | 怎么**消费**别人的库；PUBLIC/PRIVATE |
| `demo_nodes` | 三个可执行文件 | install 规则、依赖传递的终点 |

**三种接口在一个节点里各有一个，正好可以横向对比：**

| 接口 | 本工程里是哪个 | 一句话 |
|---|---|---|
| topic | `demo_interfaces/msg/Target` | 只管一直往外说，不关心谁在听 |
| service | `demo_interfaces/srv/Reset` | 一问一答，立刻返回 |
| **action** | `demo_interfaces/action/Calibrate` | **领一个长时间任务，中途汇报进度，还能被取消** |

action 在构建系统里是最容易被人想复杂的一个。本书会把它讲到底：
**它在网络上根本不存在** —— 一个 action = 3 个服务 + 2 个话题拼出来的约定。见 §2.5 和 §7。

---

## 1. 跑起来

### 1.0 两条路，选一条

本工程可以在两种环境下跑，**代码完全一样**，只是"怎么把 ROS2 备齐"不同：

| | **Ubuntu 原生**（推荐） | Windows + Docker |
|---|---|---|
| 适合 | 手边就是 Ubuntu（22.04） | 只有 Windows |
| 入口 | [ubuntu/build-and-run.sh](ubuntu/build-and-run.sh) | `docker/run.ps1` |
| 要先装 | ROS2（一条命令，见 §1.1） | Docker Desktop |
| 仓库在哪 | 你 clone 到的任意位置 | 固定挂载到容器里的 `/ws` |
| 编译快慢 | 快（原生） | 慢一些（挂载卷 + 容器） |
| 折腾程度 | 装 ROS2 那一下稍慢，之后都顺 | 拉镜像那一下可能卡（见 §1.3） |

**Ubuntu 上两条命令就能跑完：**

```bash
bash ubuntu/setup-ros2.sh        # ① 检测 + 装齐依赖（幂等，可以反复跑）
bash ubuntu/build-and-run.sh     # ② 编译 + 把五个能跑的东西全跑一遍
```

> `setup-ros2.sh` 不会盲目装东西 —— 它先检测，缺什么才装什么。
> 想先看看自己缺不缺、又不想动系统，加 `--check-only`：
>
> ```bash
> bash ubuntu/setup-ros2.sh --check-only
> ```

> ⚠️ **本书里的 `/ws/...` 路径都是 Docker 环境下的实测输出。**
> 在 Ubuntu 原生模式下，把 `/ws` 换成你的仓库根目录即可
> （本机是 `/home/byd/workspace/ros2-master`）。
> 两种模式下的**目录结构完全一致**，所以其余部分照读不误。

<details>
<summary>为什么不干脆只留一条路？</summary>

Docker 那条路本来是为了"Windows 上也能跑"。换成 Ubuntu 之后，
容器那一层反而成了**多余的中间商**：ROS2 本来就装在系统里，
再套一层容器只会让路径变长（`/ws`）、编译变慢、`ros2` 的发现机制偶尔抽风。

但 Docker 那条路不该删 —— 它对"不想动系统环境"和"Windows 用户"仍然有用。
所以两条都留着，各自独立，互不影响。
</details>

### 1.1 Ubuntu 原生路径：装 ROS2

只有在**从没装过 ROS2** 的机器上才需要看这一节。装过的直接跳到 §1.4。

```bash
bash ubuntu/setup-ros2.sh
```

这个脚本做的事，按顺序是：

1. 检查是不是 Ubuntu 22.04（jammy）—— Humble 只支持这个版本
2. 检查 ROS2 的 apt 源和 GPG key，缺了就加（默认走中科大镜像）
3. 逐项检查编译依赖，**缺什么装什么**
4. 检查 `colcon`、`ros2` 的各个子命令能不能用
5. （可选）提示你装 `rosdep`

想换镜像源：

```bash
ROS_MIRROR=tuna bash ubuntu/setup-ros2.sh     # ustc（默认）/ tuna / official
```

> **脚本里的检测逻辑有两个刻意的设计，值得注意**（它本身就当教材写）：
>
> **① 判断"装没装"要问命令，不要问包管理器。**
> `colcon` 就是个好例子：apt 装的话包名是 `python3-colcon-common-extensions`，
> pip 装的话根本没有这个 dpkg 包。本机就是 pip 装的 ——
> 只信 `dpkg -l` 的话，脚本会劝一个明明能用的机器再去装一遍。
>
> **② "缺依赖"分两种情形，装法不同。**
> 机器上压根没有 ROS2 → 装 `ros-base` 一个大礼包；
> 已经装过只是缺几个 → 只补缺的那几个。
> 判断依据是 `/opt/ros/humble/setup.bash` 在不在，
> 而不是数装了多少个包。

### 1.2 Windows / Docker 路径：环境

只需要 Docker。用官方的 ROS2 Humble 镜像：

```powershell
# 国内直连 Docker Hub 基本不通，用镜像源
docker pull docker.m.daocloud.io/ros:humble-ros-base
```

> **踩坑记录（重要）**
>
> 如果你机器上开着 Clash / v2ray 之类的系统代理，**Docker 拉镜像会全部失败**，
> 报 `error pulling image configuration: download failed after attempts=6: EOF`，
> 而且换多少个国内镜像源都没用。
>
> 原因：Docker Desktop 跑在 WSL2 里，它会把拉取流量转发给 Windows 的系统代理
> （`127.0.0.1:7890`）。于是请求从代理的**境外节点**出去，
> 国内镜像源要么拒绝（`only support mainland China`），要么连接不稳定直接 EOF。
>
> 修法：Docker Desktop → Settings → Resources → Proxies，把代理关掉改成直连。
> 因为国内镜像源本来就不需要代理。
>
> 验证方法：`docker pull` 一个 alpine 试试。能过就是好了。

### 1.3 进入容器（Windows）

在仓库根目录：

```powershell
powershell -ExecutionPolicy Bypass -File .\docker\run.ps1
```

它会挂载当前目录到容器里的 `/ws`，source 好 ROS2 环境，然后给你一个 shell。

### 1.4 编译

**Ubuntu 原生**（仓库根目录，随便哪个位置都行）：

```bash
source /opt/ros/humble/setup.bash
colcon build
source install/setup.bash
```

**Docker 里**：

```bash
cd /ws
colcon build
source install/setup.bash
```

两种模式除了 `cd` 到哪儿之外**完全一样**。

> ★ 这里有两个 `source` 是**两件不同的事**，别当成一个：
>
> | 命令 | 让当前 shell 认识什么 |
> |---|---|
> | `source /opt/ros/humble/setup.bash` | ROS2 **本身**（`ros2`、`rclcpp`、`std_msgs`……） |
> | `source install/setup.bash` | **你这个工作空间里**的包（`demo_nodes`、`demo_interfaces`……） |
>
> 顺序不能反 —— 后者要去扩展前者的环境。两个都漏了会分别报
> `ros2: command not found` 和 `Package 'demo_nodes' not found`，
> 报错长得完全不一样，但根因是同一类。
>
> Docker 模式之所以常常只看见第二个，是因为 `run.ps1` 进容器时
> 已经替你把第一个 source 过了。**换到 Ubuntu 原生，这个"隐形的步骤"没有了**，
> 每个新开的终端都得自己敲一遍。这是从 Docker 切到裸机最容易栽的跟头。

> `source install/setup.bash` 这一步新手最容易漏。见 [3.3](#33-find_package-凭什么能找到你自己写的包)。

### 1.5 五个能跑的东西

```bash
# ① 离线整定工具 —— 不是 ROS 节点，不需要起 DDS，最快能看到结果
ros2 run demo_tools tune_cli
#   也可以绕开 ros2 CLI 直接执行（它就是个普通可执行文件）：
#   ./install/demo_tools/lib/demo_tools/tune_cli

# ② 两个常驻节点一起跑，看 PID 闭环跟踪
ros2 launch demo_nodes demo.launch.py

# ③ 运行时改设定值，观察控制器重新收敛（service）
ros2 service call /reset demo_interfaces/srv/Reset "{new_setpoint: 2.0}"

# ④ 发一个 action 目标，看进度反馈和最终结果
#    另开一个终端，先按 ② 跑起 controller，然后：
ros2 action send_goal /calibrate demo_interfaces/action/Calibrate \
    "{target: 1.0, tolerance: 0.1, hold_s: 0.5, timeout_s: 25.0}" --feedback

# ⑤ 完整的 action 客户端（比 ④ 多演示"取消"）
ros2 run demo_nodes tune_client_node --ros-args -p target:=0.5 -p cancel_after_s:=2.0
```

> ⚠️ **第 ③ 条是 `/reset`，不是 `/controller/reset`。** 很多人会写错，
> 因为它看起来"应该"带节点名。实测：
> `ros2 service call /controller/reset ...` 会一直卡在 `waiting for service to become available`。
>
> 原因：相对名是拼在**命名空间**后面的，不是拼在节点名后面。
> 节点名是 `controller`，但命名空间是 `/`，所以 `"reset"` 展开就是 `/reset`。
> 想要 `/controller/reset` 得把名字写成 `"~/reset"`（`~` = 节点私有命名空间）。
> 详见 [controller_node.cpp](src/demo_nodes/src/controller_node.cpp) 里的注释。

> 注意 `tune_cli` 属于 **demo_tools** 包，不是 demo_nodes。
> `ros2 run <包名> <可执行名>` 的第一个参数是**包名**，它去 `lib/<包名>/` 下找可执行文件。
> 包名写错会报 `No executable found`。

> `tune_client_node` 的**退出码是有含义的**，可以直接用在脚本里：
> `0`=校准成功，`1`=目标被拒绝/连不上服务端，`2`=失败(超时)或被取消。

> ⚠️ **④ 和 ⑤ 同一时间只能跑一个。** 本工程的 `handleGoal` 在
> "已有任务在执行"时会拒绝新目标。所以如果你刚发完 ④ 又立刻发一次，
> 命令行会打印 `Goal was rejected.` —— 这不是坏了，是设计如此（实测）。
> 等上一条 `calibrate 成功/超时` 的日志出现再发下一条。

> 顺带一提：④ 那种"发完就 `Ctrl-C`"的做法会让服务端继续把任务跑完
> （客户端退出不等于任务取消）。想知道怎么叫停，看 ⑤ 的 `cancel_after_s`。

也可以一条命令跑完全套检查 —— 上面这五条它全替你跑一遍：

```bash
# Ubuntu 原生（脚本自己找仓库根目录，在哪儿调用都行）
bash ubuntu/build-and-run.sh

# Docker 里
bash /ws/docker/build-and-run.sh
```

> 两个脚本做的事**一模一样**，唯一的实质区别是 `ubuntu/` 那个
> 不写死 `/ws`，而是用 `${BASH_SOURCE[0]}` 自己算出仓库在哪。
> 容器里路径永远是 `/ws`，写死反而省事；裸机上仓库在哪取决于
> clone 到哪儿了，所以必须算。这个差别本身值得看一眼，
> 脚本第 20-40 行就是在讲这件事。

---

## 2. 先看产物，再看原理

**这一节建议编译完立刻做一遍**，比读十篇 CMake 教程管用。

你写的 `CMakeLists.txt` 只有几十行，但它生成的东西远比你想的多。
找到对应的产物看一眼，"CMake 在干什么"这个问题就具体了。

### 2.1 消息是怎么变成头文件的

```bash
find /ws/build/demo_interfaces -name "*__struct.hpp"
```

输出（实际编译产物）：

```
build/demo_interfaces/rosidl_generator_cpp/demo_interfaces/msg/detail/target__struct.hpp
build/demo_interfaces/rosidl_generator_cpp/demo_interfaces/srv/detail/reset__struct.hpp
build/demo_interfaces/rosidl_generator_cpp/demo_interfaces/action/detail/calibrate__struct.hpp
```

**`.msg` / `.srv` / `.action` 文件在这里被真正"翻译"成了 C++ 结构体。**
`rosidl_generate_interfaces()` 背后做的事，就是这一堆代码生成器 + 编译。

> ★ 三种接口的文件名长得一模一样（`<名字>__struct.hpp`），
> 只有中间那层目录不同（`msg/` `srv/` `action/`）。
> 这不是巧合 —— 因为 action 生成出来的东西**本来就是消息和服务**（§2.5 会证明）。

再看安装出去的部分（下游 `#include` 的实际来源）：

```bash
find /ws/install/demo_interfaces/include -name "*.hpp" | head
```

```
install/demo_interfaces/include/demo_interfaces/demo_interfaces/msg/target.hpp          ← 你 #include 的那个
install/demo_interfaces/include/demo_interfaces/demo_interfaces/msg/detail/target__struct.hpp
install/demo_interfaces/include/demo_interfaces/demo_interfaces/msg/detail/target__traits.hpp
install/demo_interfaces/include/demo_interfaces/demo_interfaces/msg/detail/target__builder.hpp
install/demo_interfaces/include/demo_interfaces/demo_interfaces/msg/detail/target__type_support.hpp
install/demo_interfaces/include/demo_interfaces/demo_interfaces/srv/reset.hpp
```

两个值得注意的点：

1. **`demo_interfaces/demo_interfaces/` 是重复的，这不是 bug。**
   外层是 `include/<包名>/`（ROS2 的约定），内层是 C++ 的 `#include <demo_interfaces/msg/target.hpp>` 命名空间。
   两套约定叠在一起就是这个效果。

2. **`detail/` 里的文件不要直接 include。** 它们是实现细节，
   跨 ROS2 版本可能变。只 include 不带 `detail` 的那层。

同时还生成了 C 版本、Python 版本、类型支持（typesupport）代码 ——
`fastrtps` / `introspection` 那几套。**同一条消息要为每种序列化后端各生成一份。**
ROS2 的消息是语言无关的，C++ 只是其中一个后端；这也是消息包编译慢的原因。

### 2.2 安装目录长什么样

```bash
find /ws/install/demo_math -type f | head -30
```

关键看这几个（**这就是"我的库怎么给别人用"的答案**，下面是实际产物）：

```
install/demo_math/
├── include/demo_math/pid.hpp                       ← 公开头文件
├── lib/libdemo_math.so                             ← 共享库
└── share/demo_math/
    ├── cmake/
    │   ├── demo_mathConfig.cmake                   ← ★ find_package 执行的入口
    │   ├── demo_mathConfig-version.cmake           ← 版本检查
    │   ├── ament_cmake_export_targets-extras.cmake ← ★ target 导出
    │   ├── ament_cmake_export_dependencies-extras.cmake ← ★ 依赖导出
    │   └── demo_mathTargetsExport.cmake            ← ★ CMake 原生的 import 定义
    ├── hook/
    │   ├── cmake_prefix_path.dsv                   ← ★ 让 CMAKE_PREFIX_PATH 能找到我
    │   └── ld_library_path_lib.dsv                 ← ★ HAS_LIBRARY_TARGET 的运行时钩子
    ├── environment/                                ← ament_prefix_path / path 等
    └── local_setup.bash 等                          ← source 时被执行的脚本
```

### 打开这几个文件读一读

**① `demo_mathTargetsExport.cmake`** —— 这是最关键的一个。核心就三行：

```cmake
add_library(demo_math::demo_math SHARED IMPORTED)
  INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include"
  INTERFACE_LINK_LIBRARIES "rclcpp::rclcpp"
```

把它和你写的 CMakeLists 对起来看：

| 你写的 | 变成 |
|---|---|
| `add_library(demo_math SHARED ...)` | `add_library(demo_math::demo_math SHARED IMPORTED)` |
| `$<INSTALL_INTERFACE:include>` | `"${_IMPORT_PREFIX}/include"` |
| `ament_target_dependencies(... PUBLIC rclcpp)` | `INTERFACE_LINK_LIBRARIES "rclcpp::rclcpp"` |

**这三个词就是"导出"的全部含义**：把我这个 target 的名字、头文件路径、
链接依赖，原样告诉下游。所谓"魔法"，其实就是一堆你可以直接读的 CMake 代码。

**② `ament_cmake_export_dependencies-extras.cmake`** ——
这是 `ament_export_dependencies(rclcpp)` 的产物，里面就是 `rclcpp` 这个名字，
以及生成 `find_dependency(rclcpp)` 的逻辑。

**③ `demo_mathConfig.cmake`** —— 入口文件。它做的事情很少：
检查重复 include、打印 `Found demo_math: 0.1.0`、然后 include 上面那几个 extras：

```cmake
set(_extras "ament_cmake_export_targets-extras.cmake;ament_cmake_export_dependencies-extras.cmake")
foreach(_extra ${_extras})
  include("${demo_math_DIR}/${_extra}")
endforeach()
```

**④ `ament_cmake_export_targets-extras.cmake`** —— 这里有个隐藏动作很值得注意。
它除了 include 上面那个 `...TargetsExport.cmake`，还会**用正则从里面把 target 名字抠出来**，
存进 `demo_math_TARGETS` 这个变量：

```cmake
list(APPEND demo_math_TARGETS ${_targets})
```

**这就是 `ament_target_dependencies(t demo_math)` 能工作的原因** ——
它查的正是 `${包名}_TARGETS` 这个变量。所以 `ament_target_dependencies`
的老式写法对**自己导出的包**同样适用，不只是系统包。

**⑤ `hook/cmake_prefix_path.dsv`** —— 全文只有一行：

```
prepend-non-duplicate;CMAKE_PREFIX_PATH;
```

**这是 `.dsv` 的真实格式**，不是"一串用冒号分隔的路径"：

```
<操作>;<环境变量名>;<相对路径>
```

意思是"把本包的前缀（`install/demo_math`）prepend 到 `CMAKE_PREFIX_PATH`"。
第三段为空 = 前缀本身。下面那个 `ld_library_path_lib.dsv` 内容是
`prepend-non-duplicate;LD_LIBRARY_PATH;lib`，意思是"把 `<前缀>/lib` 加到 `LD_LIBRARY_PATH`"。

### 2.3 colcon 到底执行了什么命令

colcon 会把每个包执行的完整命令记进日志：

```bash
cat /ws/log/latest_build/demo_math/command.log
```

这是本工程实际编译时记下的内容（**一字未改**）：

```
Invoking command in '/ws/build/demo_math':
  CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}:/opt/ros/humble
  /usr/bin/cmake /ws/src/demo_math
      -DCMAKE_INSTALL_PREFIX=/ws/install/demo_math

Invoking command in '/ws/build/demo_math':
  CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}:/opt/ros/humble
  /usr/bin/cmake --build /ws/build/demo_math -- -j12 -l12

Invoking command in '/ws/build/demo_math':
  CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}:/opt/ros/humble
  /usr/bin/cmake --install /ws/build/demo_math
```

**colcon 的全部工作就是这三条，对每个包循环一遍。** 三个关键观察：

**① 就是标准 `cmake` 三步，没有任何私有参数。**

```
cmake <源码目录> -DCMAKE_INSTALL_PREFIX=<装到哪>   # 配置
cmake --build <构建目录>                            # 编译
cmake --install <构建目录>                          # 安装
```

看明白这一点，colcon 就再也没什么神秘的了。你完全可以手动跑这三条
（见文末练习 g），效果一模一样。

**② `-DCMAKE_INSTALL_PREFIX=/ws/install/demo_math`** ——
每个包装进**自己的** `install/<包名>/`。这就是 §2.4 说的隔离安装，
也是"为什么包之间必须 `find_package`"的根源。

**③ 最关键的一行：`CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}:/opt/ros/humble`**

注意 `${CMAKE_PREFIX_PATH}` 是**原样传下去的 shell 变量引用**，
colcon 并没有把它展开成你工作空间的 `install/` 路径 ——
它只额外加了 `/opt/ros/humble`。

**那 `find_package(demo_tools)` 是怎么找到 `/ws/install/demo_tools/` 的？**

答案：靠你 `source install/setup.bash` 时**已经导出到环境里的**
`CMAKE_PREFIX_PATH`。CMake 会自动读取同名环境变量作为搜索路径的初始值。

> 所以 `colcon build` 之前如果你换了新终端、忘了 source，
> 下游包就会报 `Package 'xxx' not found` —— 因为那三个
> `install/<包名>` 路径根本没进环境。
>
> 进入容器后第一次 build 之所以能成功，是因为上游包按顺序已经装好了，
> 而 colcon 会自己把已构建包的前缀加进去。但只要你手动跑 `cmake`，
> 就必须自己保证 `CMAKE_PREFIX_PATH` 是对的。

### 2.4 一个关键细节：每个包一个独立的 CMake 工程

`colcon build` 之后，`build/` 和 `install/` 下是**每个包一个目录**：

```
build/demo_math/      install/demo_math/
build/demo_tools/     install/demo_tools/
build/demo_nodes/     install/demo_nodes/
```

不是一个大工程编译出四个包，而是 **四个独立的 CMake 工程，各自编译、各自安装**。

这个事实解释了一连串新手困惑：

| 困惑 | 答案 |
|---|---|
| 为什么每个包都要 `ament_package()`？ | 每个包都是一个独立的 CMake 工程，各自需要一个收尾动作 |
| 为什么包之间要 `find_package` 而不是 `add_subdirectory`？ | 它们不在一棵构建树里，`add_subdirectory` 够不着 |
| 为什么改了 demo_math 要重新 build 其它包？ | 包之间通过 `install/` 目录通信，不重装下游就拿到旧的头文件 |
| 为什么 `#include <demo_math/pid.hpp>` 需要 `-I` 指到 install 目录？ | 因为它就是被 install 到那儿的，源码目录根本没被引用 |

**这是理解 ROS2 构建系统最重要的一条。** 官方术语叫 isolated install
（隔离安装），`colcon build --merge-install` 可以改成合并安装，但默认是隔离的。

### 2.5 一个 `.action` 到底生成了什么

**这一节建议编译完立刻做一遍。** 它是本工程里最反直觉、也最"祛魅"的一条。

先说结论，再一条条给你证据：

> **ROS2 在通信层根本没有"action"这个原语，只有 topic 和 service。
> 一个 action = 3 个服务 + 2 个话题。**

听起来像暴论，但下面四条全是本工程实际编译 + 实际运行出来的结果。

#### 证据 ① 一个 `.action` 生成了 7 个类型

打开生成的 C++ 头文件，在 `demo_interfaces::action` 命名空间下能找到这些类型：

```
Calibrate_Goal              ← Goal 段，本质就是个普通消息
Calibrate_Result            ← Result 段
Calibrate_Feedback          ← Feedback 段
Calibrate_SendGoal          ← 服务：客户端提交目标
Calibrate_GetResult         ← 服务：客户端取回最终结果
Calibrate_FeedbackMessage   ← 消息：反馈的**内部**载体
Calibrate                   ← 门面：把上面这些串起来
```

`.action` 里只写了 3 段，怎么冒出 2 个服务和 1 个额外消息的？
因为**发送目标、取回结果在协议上就是两次服务调用**。

#### 证据 ② 生成的门面结构体，等于一份自白书

[install/…/action/detail/calibrate__struct.hpp](src/demo_interfaces/action/Calibrate.action)
的最后 30 行，长这样（**一字未改，来自实际产物**）：

```cpp
struct Calibrate
{
  using Goal     = demo_interfaces::action::Calibrate_Goal;
  using Result   = demo_interfaces::action::Calibrate_Result;
  using Feedback = demo_interfaces::action::Calibrate_Feedback;

  struct Impl
  {
    using SendGoalService   = demo_interfaces::action::Calibrate_SendGoal;
    using GetResultService  = demo_interfaces::action::Calibrate_GetResult;
    using FeedbackMessage   = demo_interfaces::action::Calibrate_FeedbackMessage;

    using CancelGoalService = action_msgs::srv::CancelGoal;          // ★ 借来的
    using GoalStatusMessage = action_msgs::msg::GoalStatusArray;     // ★ 借来的
  };
};
```

看最后两行 —— 它们**不是** `Calibrate_*` 开头，而是 `action_msgs::*`。

**"取消"和"状态"这两件事根本没有按 action 各生成一份，
而是所有 action 共用 `action_msgs` 里同一套通用服务/话题。**

这直接解释了 §3.5 里那条"依赖要写两处"，也解释了为什么
`demo_interfaces` 会依赖 `action_msgs` 这个看起来跟本工程毫无关系的包。

> ★ 一个诚实的补充：实测发现 `rosidl_generate_interfaces()` 会**自己**去
> `find_package(action_msgs)` 并把它加进依赖（源码见 §7.4）——
> 所以本工程里手写的那行 `find_package(action_msgs REQUIRED)`
> 严格说是**冗余**的。但**它必须存在**这件事是真的：
> 只是"由谁写"由宏代劳了而已。手写出来，依赖关系才是可见的。

#### 证据 ③ 运行时看网络：3 个服务 + 2 个话题

跑起 `controller_node`，然后把**隐藏名**也列出来：

```bash
ros2 service list --include-hidden-services | grep calibrate
ros2 topic   list --include-hidden-topics   | grep calibrate
```

实测输出：

```
/calibrate/_action/cancel_goal      ← action_msgs/srv/CancelGoal   （通用，借来的）
/calibrate/_action/get_result       ← Calibrate_GetResult          （生成的）
/calibrate/_action/send_goal        ← Calibrate_SendGoal           （生成的）

/calibrate/_action/feedback         ← Calibrate_FeedbackMessage    （生成的）
/calibrate/_action/status           ← action_msgs/msg/GoalStatusArray（通用，借来的）
```

**网络上找不到任何"action"字样。** 五个实体，全是服务和话题。

> 为什么默认看不见？因为它们挂在 `_action/` 这个"隐藏"命名空间下，
> 得加 `--include-hidden-*`。这也是从没注意过这件事的人这么容易忽略它的原因。

> ★ **"五个"这件事本身就是证据**：`rcl_action_server_init()` 一上来就把
> 3 个服务和 2 个发布者全建好了 —— 不需要等第一个 goal 到来。
> 实测（在全新的 `ROS_DOMAIN_ID` 上，从未发过任何 goal）：
>
> ```
> $ ros2 service list --include-hidden-services | grep calibrate
> /calibrate/_action/cancel_goal
> /calibrate/_action/get_result
> /calibrate/_action/send_goal
> $ ros2 topic list --include-hidden-topics | grep calibrate
> /calibrate/_action/feedback
> /calibrate/_action/status
> $ ros2 topic info /calibrate/_action/status
> Publisher count: 1        ← 没人订阅，但发布者确实在
> ```
>
> 也就是说 action 服务端**从启动那一刻起就在网络上占了这五个名字**，
> 和有没有任务无关。这从侧面说明了"action 是一组通信实体的固定组合"，
> 而不像 service 那样是"一个名字，用时才亮"。

顺便对比一下 `ros2 action list` 和 `ros2 node info` 的"高层视角"：

```
$ ros2 action list -t
/calibrate [demo_interfaces/action/Calibrate]

$ ros2 node info /controller
  Service Servers:
    /reset: demo_interfaces/srv/Reset
  Action Servers:
    /calibrate: demo_interfaces/action/Calibrate
```

**同一件东西，两个视角。** 高层视角里它是"一个 action"；
底层视角里它是"三个服务 + 两个话题"。两句话都对，只是抽象层次不同。

#### 证据 ④ 文件数量：`.action` 和 `.msg` 完全一样

这条最简单粗暴，也最有说服力。数一数每种接口生成的头文件个数：

```bash
for n in target reset calibrate; do
  printf "%-10s %s\n" "$n" "$(find install/demo_interfaces/include -iname "${n}*" | wc -l)"
done
```

实测输出：

```
target     16
reset      16
calibrate  16
```

**三个数字一模一样，都是 16。**

明明 `.action` 里塞了三段（Goal + Result + Feedback），
生成的文件个数却和一个 `.msg` 完全相同 —— 因为
`rosidl_adapter` 把整个 `.action` 翻成**一个** IDL 文件：

```bash
cat build/demo_interfaces/rosidl_adapter/demo_interfaces/action/Calibrate.idl
#   module demo_interfaces { module action {
#     struct Calibrate_Goal     { ... };
#     struct Calibrate_Result   { ... };
#     struct Calibrate_Feedback { ... };
#   } }
```

三个 struct 挤在同一个 IDL 里，之后**所有生成器（C / C++ / Python /
各种 typesupport）都是同一套流程，和 msg 没有任何区别**。

#### 这一节对 CMake 的三条实用推论

| 现象 | 原因 |
|---|---|
| 为什么 action 不需要单独的生成宏？`rosidl_generate_interfaces` 里它和 `.msg` 平起平坐 | 它生成出来的本来就是消息和服务，走同一条代码路径 |
| 为什么 `action_msgs` 会进依赖？ | 生成的 `SendGoal` / `GetResult` / `FeedbackMessage` 里嵌了 `action_msgs` 的类型 |
| 为什么"定义 action"**不需要** `find_package(rclcpp_action)`？ | 这里只生成**数据结构**。`rclcpp_action` 是写 server/client 的库，属于 `demo_nodes` 的依赖 |

**一句话记住：`action_msgs` 是"定义接口"的依赖，`rclcpp_action` 是"写节点"的依赖。**
action 相关的 CMake 报错，十有八九是把这两个写错了地方（见 §8 报错对照表）。

> ★ 但别把上面那张表**背成教条**。实测发现（详见 §7.4）：
> `action_msgs` 那一行在 Humble 里是宏**自动**完成的 ——
> 手动删掉 `find_package(action_msgs)` 和 `DEPENDENCIES` 里的它，
> 编译照样过。真正会炸的是 `rclcpp_action` 那一行，以及
> "`DEPENDENCIES` 里写了却没 `find_package`"这种**配套**问题。
>
> **结论是**：分清"哪个包属于哪一侧"依然是对的心智模型，
> 但"漏了就会报错"这个推断，只有对**一部分**依赖成立。
> 想知道自己有没有漏，答案不在编译器里，在 `rosdep check` 里。

---

## 3. ROS2 和 CMake 到底是什么关系

### 3.1 三层结构

```
┌──────────────────────────────────────────────────────┐
│  colcon           工作空间编排器                       │
│                   "要构建哪些包？按什么顺序？"          │
│                   读 package.xml 建依赖图，拓扑排序      │
└────────────────────────┬─────────────────────────────┘
                         │ 对每个包，调用 ↓
┌────────────────────────▼─────────────────────────────┐
│  ament_cmake       CMake 的宏库 + 一套约定             │
│                   "ROS2 的包长什么样、怎么导出、怎么装"  │
│                   提供 ament_package() 等 ~20 个宏      │
└────────────────────────┬─────────────────────────────┘
                         │ 宏展开成 ↓
┌────────────────────────▼─────────────────────────────┐
│  CMake             真正的构建系统                       │
│                   target / 属性 / install / export     │
│                   生成 Makefile 或 Ninja               │
└────────────────────────┬─────────────────────────────┘
                         │ 调用 ↓
                    ┌────▼────┐
                    │  g++    │
                    └─────────┘
```

要点：

- **colcon 不是构建系统**，它是构建系统的调度器。你不用 colcon，
  手动 `cd build/demo_math && cmake ... && make install` 一样能编，
  只是要自己处理包之间的顺序和 `CMAKE_PREFIX_PATH`。
- **ament_cmake 不是另一个 CMake**，它是一批 `.cmake` 宏文件。
  `find_package(ament_cmake)` 之后，那些 `ament_*` 命令才被定义出来。
- **CMake 才是真正干活的那个**。你写的所有东西最终都是 CMake 语法。

### 3.2 一个 ROS2 C++ 包的完整生命周期

```
                    src/demo_math/
                    ├── package.xml        ┐
                    └── CMakeLists.txt     ┘
                             │
   ┌─────────────────────────┴──────────────────────────┐
   │ ① colcon 读 src/ 下所有 package.xml                 │
   │    → 建依赖图：demo_interfaces → demo_math → ...    │
   │    → 拓扑排序，决定构建顺序                          │
   └─────────────────────────┬──────────────────────────┘
                             │
   ┌─────────────────────────▼──────────────────────────┐
   │ ② cmake 配置阶段                                    │
   │    find_package(ament_cmake)  ← 注入 ament_* 宏     │
   │    add_library(...)           ← 造 target          │
   │    target_*()                 ← 给 target 设属性    │
   │    install(...)               ← 定安装规则          │
   │    ament_export_targets(...)  ← 攒导出内容          │
   │    ament_package()            ← 把上面的写成文件     │
   └─────────────────────────┬──────────────────────────┘
                             │
   ┌─────────────────────────▼──────────────────────────┐
   │ ③ cmake --build   → 调 g++，产出 libdemo_math.so    │
   └─────────────────────────┬──────────────────────────┘
                             │
   ┌─────────────────────────▼──────────────────────────┐
   │ ④ cmake --install → 文件落到 install/demo_math/     │
   │    include/  lib/  share/demo_math/cmake/           │
   └─────────────────────────┬──────────────────────────┘
                             │
   ┌─────────────────────────▼──────────────────────────┐
   │ ⑤ source install/setup.bash                         │
   │    → AMENT_PREFIX_PATH += install/demo_math         │
   │    → CMAKE_PREFIX_PATH += 同上                      │
   │    → LD_LIBRARY_PATH  += install/demo_math/lib      │
   └─────────────────────────┬──────────────────────────┘
                             │
   ┌─────────────────────────▼──────────────────────────┐
   │ ⑥ 下游包 find_package(demo_math)                    │
   │    → 在 CMAKE_PREFIX_PATH 下找到 Config.cmake       │
   │    → 执行它，得到 imported target demo_math::demo_math│
   └────────────────────────────────────────────────────┘
```

**②→④→⑤→⑥ 这条链，就是「ROS2 和 CMake 的关系」的全部答案。**

### 3.3 `find_package` 凭什么能找到你自己写的包

标准的 CMake `find_package` 靠 `CMAKE_PREFIX_PATH` 找包。它会在每个前缀下
按固定顺序搜索，其中一条是：

```
<prefix>/share/<包名>/cmake/<包名>Config.cmake
```

而 `ament_package()` 恰好就是把文件装到这个位置。验证一下：

```bash
ls install/
# COLCON_IGNORE  demo_interfaces  demo_math  demo_nodes  demo_tools
# local_setup.bash  setup.bash  _local_setup_util_sh.py  ...

source install/setup.bash
echo $CMAKE_PREFIX_PATH | tr ':' '\n' | head -4
```

实测输出：

```
/ws/install/demo_nodes
/ws/install/demo_tools
/ws/install/demo_math
/ws/install/demo_interfaces
```

**四个 install 前缀全部进了 `CMAKE_PREFIX_PATH`，然后 CMake 就能找到了。** 完整链条：

```
每个包里的 hook/cmake_prefix_path.dsv
  内容: prepend-non-duplicate;CMAKE_PREFIX_PATH;
                │
                ▼
       source install/setup.bash
       （由 _local_setup_util_sh.py 收集所有 .dsv 钩子并执行）
                │
                ▼
  CMAKE_PREFIX_PATH += /ws/install/demo_math
                │
                ▼
  find_package(demo_math) 在 CMAKE_PREFIX_PATH 下搜索
                │
                ▼
  命中 /ws/install/demo_math/share/demo_math/cmake/demo_mathConfig.cmake
                │
                ▼
  得到 imported target: demo_math::demo_math
```

注意 `ament_cmake_DIR` 也是同一个规律，编译时实际值是：

```
ament_cmake_DIR:PATH=/opt/ros/humble/share/ament_cmake/cmake
rclcpp_DIR:PATH=/opt/ros/humble/share/rclcpp/cmake
```

**系统包和你自己写的包，在 CMake 眼里走的是完全相同的搜索逻辑** ——
区别只是前缀一个是 `/opt/ros/humble`，一个是 `/ws/install/<包名>`。

**这就是为什么 `colcon build` 之后必须 `source install/setup.bash`。**
不 source，`CMAKE_PREFIX_PATH` 里就没有你自己包的路径，
下游 `find_package(demo_tools)` 会直接报找不到包。

同时也是为什么"新开一个终端要重新 source" —— 环境变量是 per-shell 的。

### 3.4 package 和 target 是两个粒度

ROS2 里到处是这两个词，混用会让人很晕。它们的对应关系是：

| 概念 | 属于谁 | 例子 | 作用 |
|---|---|---|---|
| **package** | ROS2 / colcon | `demo_math` | 构建的**调度单位**。有 package.xml，有版本号，能被 rosdep 管理 |
| **target** | CMake | `demo_math`、`demo_math::demo_math`、`demo_tools_internal` | 编译的**执行单位**。有属性、有依赖关系 |

一个 package 里可以有任意多个 target：

```
demo_tools 这个 package 里有 2 个 target：
    demo_tools              ← SHARED 库，被 install + export
    demo_tools_internal     ← STATIC 库，不 install、不 export
    tune_cli                ← 可执行文件，被 install，不进 export 集
```

反过来，一个 target 也可能来自别的 package（imported target）。

**判断标准**：
- 要说"谁能被 rosdep 装"、"谁先编译" → 说的是 package
- 要说"链接谁"、"头文件路径怎么传" → 说的是 target

`demo_tools` 这个包里那个 `demo_tools_internal`，在 `package.xml` 里
**根本不存在** —— 因为它不是 ROS 包，只是个 CMake target。
这个例子最能说明两者的粒度差异。

### 3.5 完整的依赖声明要写几个地方

这是 ROS2 新手最容易出错的地方。一个依赖关系，往往要在**三个地方**各声明一次：

| 位置 | 给谁看 | 漏了会怎样 |
|---|---|---|
| `package.xml` | colcon / rosdep / ros2 CLI | 构建顺序错乱，或用 `ros2 run` 时报找不到依赖 |
| `find_package()` | CMake 配置阶段 | `target not found`、找不到头文件 |
| `target_link_libraries` / `ament_target_dependencies` | CMake 生成阶段 | 编译过、**链接**失败（undefined reference） |

以 `demo_nodes` 依赖 `demo_interfaces` 为例：

```xml
<!-- package.xml -->
<depend>demo_interfaces</depend>
```
```cmake
# CMakeLists.txt
find_package(demo_interfaces REQUIRED)
ament_target_dependencies(controller_node demo_interfaces)
```

**三个地方缺一不可，而且不能互相替代。** 这跟纯 CMake 项目不一样 ——
纯 CMake 项目没有 `package.xml` 这一层。

> 常见症状对照：
> - 编译期 `fatal error: xxx.hpp: No such file` → `find_package` 或 include 目录的问题
> - 链接期 `undefined reference to xxx` → `target_link_libraries` 漏了
> - 启动时 `Package 'xxx' not found` → `package.xml` 或没 source

### 3.6 package.xml 和 CMakeLists.txt 到底是怎么接起来的

`project()` 是 CMake 的命令，`package.xml` 是 ROS 的元数据格式 ——
**它们确实是两套完全不同的体系，互相不认识。** CMake 没有 XML 解析能力，
ROS 也不管你用什么构建系统。

那 ROS2 的包是怎么让这两个世界对话的？靠 `ament_cmake` 里的一层**翻译**。

#### 版本号就是最好的例子

`demo_interfaces/CMakeLists.txt` 里写的是 `project(demo_interfaces)` ——
**没有** `VERSION` 参数。按 CMake 的原生规则，`PROJECT_VERSION` 就是空的：

```cmake
project(demo)              → PROJECT_VERSION = ""        （空）
project(demo VERSION 0.1.0) → PROJECT_VERSION = "0.1.0"
```

实测三个版本变量的真实取值：

| 变量 | 值 | 谁设置的 |
|---|---|---|
| `PROJECT_VERSION` | **空** | CMake 的 `project()`，因为没传 VERSION |
| `demo_interfaces_VERSION` | `0.1.0` | ★ **ament 从 package.xml 解析出来的** |
| `PACKAGE_VERSION` | 空（在 `ament_package()` 内部才赋值） | `ament_package()` |

注意真正的那个变量名是 **`<包名>_VERSION`**，不是 `PROJECT_VERSION`。

#### 翻译是怎么发生的

```
              package.xml
        <name>demo_interfaces</name>
        <version>0.1.0</version>
                   │
                   │  ← ament_package_xml() 调用一个 Python 脚本:
                   │     share/ament_cmake_core/cmake/core/package_xml_2_cmake.py
                   ▼
    build/<包名>/ament_cmake_core/package.cmake      ← 中间产物，可以直接打开看
        set(_AMENT_PACKAGE_NAME "demo_interfaces")
        set(demo_interfaces_VERSION "0.1.0")         ← ★ 版本号在这里变成 CMake 变量
        set(demo_interfaces_MAINTAINER "Your Name <you@example.com>")
                   │
                   │  ← include() 进来
                   ▼
             CMake 的世界
```

**所以 ament_cmake 的本质，就是"一个把 package.xml 翻译成 CMake 变量的翻译层"。**
CMake 不认识 XML，package.xml 也不认识 CMake，两边靠一个 Python 脚本接起来。

#### 两个可以直接验证的推论

**① `project()` 的名字必须和 `package.xml` 的 `<name>` 一致。**
不一致会直接报错（已实测）：

```
CMake Error at .../ament_package_xml.cmake:53 (message):
  ament_package_xml() package name 'demo_interfaces' in '/package.xml' does
  not match current PROJECT_NAME 'oops_wrong_name'.  You must call project()
  with the same package name before.
```

因为翻译出来的变量名是 `<包名>_VERSION` ——
**包名是那个变量名的一部分**，两边不一致它就没法把版本号拼出来。

**② 版本号只需要写在一处。** 把 `package.xml` 的 `<version>` 改成 `9.9.9`，
`demo_interfaces_VERSION` 跟着变成 `9.9.9`（已实测），
生成的 `demo_interfacesConfig-version.cmake` 也会跟着变，
于是下游 `find_package(demo_interfaces 9.0)` 的版本检查行为也变了。

#### 那纯 CMake（非 ROS）场景呢？

没有 `package.xml`、没有 ament、没有这层翻译，就只剩 CMake 原生规则：

```cmake
project(my_lib VERSION 1.2.3)   # ← 版本号只能写在这里
```

不写 `VERSION` 就是空字符串。版本号从哪来、怎么和别处保持一致，
**全是你的问题**，CMake 不管。常见的几种做法：

| 做法 | 说明 |
|---|---|
| 写在 `cmake_minimum_required` 下面 | 最朴素，但和文档/包管理器里的版本号容易脱节 |
| 读 `VERSION` 文件 | `file(READ VERSION v)`，单一数据源 |
| 从 git 取 | `execute_process(COMMAND git describe --tags)` |
| 用 `configure_file()` 生成头文件 | 让 C++ 代码也能拿到版本号：`#define MYLIB_VERSION "@PROJECT_VERSION@"` |

**回头看 ROS2：package.xml 扮演的其实就是那个"单一数据源"角色**，
只是 ament 替你写好了翻译层。

> **一句总结**：`package.xml` 负责"我是谁、我依赖谁、什么版本"（给 colcon/rosdep 看），
> `CMakeLists.txt` 负责"怎么编译链接"（给 CMake 看）。
> `ament_cmake` 不是第三套体系，它就是这两者之间的胶水。

### 3.7 `source setup.bash` 到底做了什么

你每开一个终端都要敲一次，但它到底干了什么，很多人答不上来。

**它不是程序，是一段 shell 脚本，用 `source`（= `.`）在当前 shell 里执行。**
（`source` 和直接跑的区别：前者改的是**当前** shell 的环境变量，后者改的是一个立即退出的子进程。）

#### 实测：干净环境里，它设置了 8 个变量

用 `env -i` 起一个完全干净的环境，然后 source：

| 变量 | 值 |
|---|---|
| `ROS_VERSION` | `2` |
| `ROS_DISTRO` | `humble` |
| `ROS_PYTHON_VERSION` | `3` |
| `ROS_LOCALHOST_ONLY` | `0` |
| `AMENT_PREFIX_PATH` | `/opt/ros/humble` |
| `PATH` | `/opt/ros/humble/bin:...` |
| `LD_LIBRARY_PATH` | `/opt/ros/humble/lib/x86_64-linux-gnu:/opt/ros/humble/lib` |
| `PYTHONPATH` | `/opt/ros/humble/lib/python3.10/site-packages:...` |

这 8 个变量正好对应四件事：

- **`PATH`** → 让 `ros2` / `colcon` / `rosdep` 这些命令能用（可执行文件在 `bin/`）
- **`LD_LIBRARY_PATH`** → 程序启动时能找到 `librclcpp.so` 等 .so
- **`PYTHONPATH`** → Python 能 `import rclpy`
- **`AMENT_PREFIX_PATH`** → **ROS2 自己的"前缀列表"**，`ros2 run` / `ros2 launch` / `ros2 pkg` 全靠它定位包
- `ROS_*` 那几个是给运行时和工具链读的标识

#### 注意：`CMAKE_PREFIX_PATH` **不在**这 8 个里面

实测 source 完 `/opt/ros/humble/setup.bash` 之后：

```
CMAKE_PREFIX_PATH=[]        ← 空的
COLCON_PREFIX_PATH=[]       ← 空的
```

那编译时 CMake 怎么找到 `/opt/ros/humble` 的？**是 colcon 在调用 cmake 时显式加的**
（见 §2.3 的真实命令）：`CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}:/opt/ros/humble`。

**`CMAKE_PREFIX_PATH` 是"构建期"的事，`AMENT_PREFIX_PATH` 是"运行期"的事** ——
两个变量、两套机制、两个阶段。这是个很容易混的地方。

`CMAKE_PREFIX_PATH` 真正被填上，是在你 `source install/setup.bash` 的时候
（工作空间里每个包都提供了 `hook/cmake_prefix_path.dsv`，见 §3.3）。

#### 完整调用链

```
source /opt/ros/humble/setup.bash
  │  ① 只是个转发器，全文 5 行：
  │     AMENT_SHELL=bash
  │     AMENT_CURRENT_PREFIX=$(dirname $BASH_SOURCE)
  │     . "$AMENT_CURRENT_PREFIX/setup.sh"
  ▼
setup.sh
  │  ② 组装"要激活哪些 prefix"的列表：
  │     - 读 share/ament_index/resource_index/parent_prefix_path/*  ← 父前缀链
  │     - 再加上自己
  │     - 去重后逐个 . $prefix/local_setup.$AMENT_SHELL
  │     （`.sh` 版本是 shell 无关的，bash/zsh 各有一个薄封装）
  ▼
local_setup.sh
  │  ③ 调用 Python 脚本生成 shell 代码，然后 eval：
  │     _ament_ordered_commands="$($_ament_python_executable \
  │         "$AMENT_CURRENT_PREFIX/_local_setup_util.py" sh)"
  │     eval "${_ament_ordered_commands}"
  ▼
_local_setup_util.py
  │  ④ 扫描所有 .dsv 钩子文件，按算法排好序，输出 export 语句：
  │       share/*/environment/*.dsv     ← 环境变量
  │       share/*/hook/*.dsv            ← 钩子
  │     实测 /opt/ros/humble 下一共有 681 个 .dsv 文件
  ▼
.dsv 文件（一行一个指令，格式见 §5.4）
      environment/ament_prefix_path.dsv -> prepend-non-duplicate;AMENT_PREFIX_PATH;
      environment/path.dsv              -> prepend-non-duplicate-if-exists;PATH;bin
```

**看明白这条链，几个常见困惑就自动解开了：**

| 困惑 | 答案 |
|---|---|
| 为什么必须 `source` 而不是 `./setup.bash`？ | 后者在子进程里改环境，子进程一退出就没了 |
| 为什么新开终端要重新 source？ | 环境变量是 per-shell 的，新 shell 继承自父 shell，但没有你的 source 结果 |
| 为什么 `source install/setup.bash` 之后 `/opt/ros/humble` 也生效了？ | `parent_prefix_path` 资源记录了父前缀链，overlay 会自动把底层一起激活 |
| 为什么 `source install/setup.bash` **之前**要先 source `/opt/ros/humble/setup.bash`？ | 严格说不是必须的 —— overlay 会通过 `parent_prefix_path` 找到底层并替你 source。但顺序反了、或者两个工作空间互相覆盖时容易出怪问题，所以习惯上还是从底层往上层依次 source |
| `ros2 run` 怎么找到可执行文件的？ | 在 `AMENT_PREFIX_PATH` 的每个前缀下找 `lib/<包名>/<可执行名>` |

#### 调试手段：让它把执行过程打出来

```bash
export AMENT_TRACE_SETUP_FILES=1
source /opt/ros/humble/setup.bash
```

输出会告诉你每一个被 source 的文件：

```
# . "/opt/ros/humble/setup.sh"
# . "/opt/ros/humble/local_setup.bash"
# . "/opt/ros/humble/local_setup.sh"
...
```

**环境变量莫名不对时（比如 `ros2 run` 找不到包、找不到 .so），
第一件事就是开这个开关，看哪些 setup 文件真的被执行了。**

---

## 4. CMake 语法速查

本工程用到的每一条命令，按"重要程度"排序。

### 4.1 现代 CMake 的核心：一切皆 target

这是从"老 CMake"跨到"现代 CMake"的分水岭：

```cmake
# ✗ 老写法：一堆全局变量，靠目录继承，靠运气
include_directories(${SOME_INCLUDE_DIRS})
link_libraries(${SOME_LIBRARIES})

# ✓ 新写法：一切挂在 target 上，依赖关系显式且可传递
target_include_directories(mylib PUBLIC ...)
target_link_libraries(mylib PUBLIC other)
```

为什么新写法好：**依赖关系变成 target 的属性，可以精确控制传播范围。**

### 4.2 PUBLIC / PRIVATE / INTERFACE —— 必须背下来

```
                     ┌──────────────┬──────────────┐
                     │  本 target   │   下游       │
   ┌─────────────────┼──────────────┼──────────────┤
   │ PRIVATE         │      ✓       │      ✗       │
   │ PUBLIC          │      ✓       │      ✓       │
   │ INTERFACE       │      ✗       │      ✓       │
   └─────────────────┴──────────────┴──────────────┘
```

判断标准只有一句话：

> **出现在你的公开头文件里 → PUBLIC；只出现在 .cpp 里 → PRIVATE。**

具体到本工程：

```cmake
# demo_tools/CMakeLists.txt
target_link_libraries(demo_tools
  PUBLIC  demo_math::demo_math    # auto_tuner.hpp 里用了 demo_math::Pid → 下游也要
  PUBLIC  rclcpp::rclcpp          # auto_tuner.hpp 里用了 rclcpp::Duration → 下游也要
  PRIVATE demo_tools_internal     # ring_buffer 被 pimpl 藏在 .cpp 里 → 下游不需要
)
```

`demo_tools` 用到的 `INTERFACE` 场景：纯头文件库（本工程没有用到，但很常见）。

**证据**：编译完之后，去读 CMake 生成的导出文件，
`demo_tools` 被 install 出去的接口属性是这样的：

```bash
grep -E "INTERFACE_(LINK_LIBRARIES|INCLUDE_DIRECTORIES)" \
     install/demo_tools/share/demo_tools/cmake/demo_toolsTargetsExport.cmake
```

```
INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include"
INTERFACE_LINK_LIBRARIES "demo_math::demo_math;rclcpp::rclcpp"
```

**注意 `demo_tools_internal` 去哪了 —— 它根本不在这行里。**

这就是 `PRIVATE` 的全部含义在 CMake 内部的体现：
它没被写进 `INTERFACE_LINK_LIBRARIES`，所以下游**永远不会**看到它，
也不会有人去 `find_package` 它。链接关系到这里就断了。

对照 `demo_math` 的导出（它只有一个 PUBLIC 依赖 rclcpp）：

```
INTERFACE_LINK_LIBRARIES "rclcpp::rclcpp"
```

**一句话总结**：`PUBLIC` / `PRIVATE` 的区别，最终就落在这一个 CMake 变量
`INTERFACE_LINK_LIBRARIES` 里有没有那个名字。

**自己动手验证**：把 `PUBLIC demo_math::demo_math` 挪到 `PRIVATE`，重新
`colcon build`。实测会在 **`demo_tools` 自己**就编译失败：

```
demo_tools/src/tune_cli.cpp:25:10:
    fatal error: demo_math/pid.hpp: No such file or directory
```

为什么是本包内的 `tune_cli` 先炸？因为 **`PUBLIC` / `PRIVATE` 的作用域是
"谁链接了这个 target"，不是"哪个包"** —— 同一个包里的可执行文件，
对库来说同样是"下游"。这个细节很多人会想错，做一次就明白了。

### 4.3 生成器表达式 `$<...>`

```cmake
target_include_directories(demo_math PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
```

解决的问题：**头文件在编译期和安装后是两个不同的路径。**

```
编译期（还在工作空间里）: /ws/src/demo_math/include
安装后（下游引用）:       <install_prefix>/include
```

写死任何一个，另一半场景就会挂。`$<...>` 是**延迟求值**：
CMake 生成构建系统时才决定用哪个。

- `$<BUILD_INTERFACE:path>` —— 在这棵构建树内部使用时取它
- `$<INSTALL_INTERFACE:path>` —— 被 install 后下游使用时取它

**注意 `INSTALL_INTERFACE` 里必须写相对路径。** 写绝对路径
（比如 `/home/you/ws/install/demo_math/include`）在你自己机器上能跑，
别人一拉代码就编不过。

### 4.4 `install()` —— 从"能编"到"能被别人用"

```cmake
install(TARGETS demo_math
  EXPORT demo_mathTargets        # 把 target 属性记进导出集
  ARCHIVE DESTINATION lib        # 静态库 .a  → lib/
  LIBRARY DESTINATION lib        # 动态库 .so → lib/
  RUNTIME DESTINATION bin        # 可执行文件 → bin/
)
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})
```

Linux 上三者的对应关系：

| 参数 | 文件类型 | 本工程里的例子 |
|---|---|---|
| `RUNTIME` | 可执行文件 | `controller_node` |
| `LIBRARY` | 动态库 `.so` | `libdemo_math.so` |
| `ARCHIVE` | 静态库 `.a` | `libdemo_tools_internal.a` |

三个都写是为了跨平台（Windows 上 `.dll` 算 `RUNTIME`，会落到 `bin/`）。

**两个高频坑**：

1. **`DESTINATION` 末尾斜杠**：
   - `install(DIRECTORY launch DESTINATION share/x)` → `share/x/launch/...` ✓
   - `install(DIRECTORY launch/ DESTINATION share/x)` → `share/x/demo.launch.py` ✗

   带斜杠 = 拷**里面的内容**；不带 = 拷**这个目录本身**。

2. **ROS2 的 `ros2 run` 只认 `lib/<包名>/`**：
   ```cmake
   install(TARGETS controller_node DESTINATION lib/${PROJECT_NAME})  # ✓
   install(TARGETS controller_node DESTINATION bin)                   # ✗ ros2 run 找不到
   ```
   后者编译安装都成功，只有 `ros2 run` 报 `No executable found`，很难查。

### 4.5 `find_package` 的两个关键词

```cmake
find_package(ament_cmake REQUIRED)               # 找不到就 FATAL_ERROR
find_package(rclcpp REQUIRED COMPONENTS ...)     # ROS2 的包一般不用 COMPONENTS
find_package(OpenCV 4.5 REQUIRED)                # 带版本要求
```

- `REQUIRED`：**永远加上**。不加的话找不到包也继续往下走，
  最后在链接阶段炸，报错信息离真正的原因十万八千里。
- `QUIET`：找不到时不打印搜索过程，不太需要。

`find_package` 做的两件事：
1. 定义 imported target（`rclcpp::rclcpp`）
2. 设置变量（`rclcpp_INCLUDE_DIRS`、`rclcpp_LIBRARIES`、`rclcpp_FOUND`）

现代写法用 target，老写法用变量。ament 的宏两种都兼容。

### 4.6 几个容易忽略的

```cmake
cmake_minimum_required(VERSION 3.8)   # 决定可用语法 + policy 默认值
project(demo_math)                     # 定义 PROJECT_NAME 等变量

if(NOT CMAKE_CXX_STANDARD)             # ★ 只提供默认值，不覆盖用户设置
  set(CMAKE_CXX_STANDARD 17)
endif()

target_compile_features(lib PUBLIC cxx_std_17)   # ★ 更精确: 跟着 target 传下去

set_target_properties(t PROPERTIES POSITION_INDEPENDENT_CODE ON)  # 静态库进共享库必须
```

`CMAKE_CXX_STANDARD` 是缓存变量，可能被 colcon / 用户 / 别的包设过。
直接 `set()` 会覆盖掉用户的选择 —— **库代码不要强改全局设置。**

`target_compile_features` 比全局变量好：它跟着 target 走，
下游哪怕自己设了 C++14，链接本库时也会被抬到 C++17。

---

## 5. ament_cmake 语法速查

### 5.1 全部核心宏

| 宏 | 等价的标准 CMake | 作用 |
|---|---|---|
| `ament_package()` | 无直接等价物 | 每个包最后一行。生成 `Config.cmake` 等 |
| `ament_target_dependencies(t PUBLIC rclcpp)` | `target_link_libraries(t PUBLIC rclcpp::rclcpp)` + include 目录 | 链接一个 ROS 包 |
| `ament_export_targets(Targets HAS_LIBRARY_TARGET)` | `install(EXPORT ...)` + 命名空间 | 把自己的 target 导出给下游 |
| `ament_export_dependencies(rclcpp)` | 无（生成 `find_dependency`） | 让下游自动 `find_package` 我依赖的包 |
| `rosidl_generate_interfaces(...)` | 几十行代码生成 + install | 把 `.msg`/`.srv` 变成 C++ 库 |
| `ament_export_include_directories(...)` | ⚠️ 老写法，新代码别用 | 导出 include 目录 |
| `ament_export_libraries(...)` | ⚠️ 老写法，新代码别用 | 导出库 |
| `ament_auto_find_build_dependencies()` | 一堆 `find_package` | 从 package.xml 自动推导要 find 什么 |

### 5.2 `ament_target_dependencies` 到底做了什么

不用猜，可以直接读源码
（`ament_cmake_target_dependencies/cmake/ament_target_dependencies.cmake`）。
它是这样工作的：

1. 对每个包名，检查 `${包名}_TARGETS`（比如 `rclcpp::rclcpp`）是否存在
2. **有现代 target** → 用 `target_link_libraries()` 链接它
3. **没有** → 退回用老变量 `rclcpp_INCLUDE_DIRS` / `rclcpp_LIBRARIES`，
   挨个 `target_include_directories()` / `target_link_libraries()`

所以：

```cmake
ament_target_dependencies(t PUBLIC rclcpp)
# 约等于
target_link_libraries(t PUBLIC rclcpp::rclcpp)
target_include_directories(t PUBLIC ${rclcpp_INCLUDE_DIRS})   # 现代包里这步是空的
```

**它不是新机制，就是标准命令的打包。** 本工程两种写法都有：

- `demo_math` 用 `ament_target_dependencies`（ROS2 主流写法，看懂别人代码必需）
- `demo_tools` 用 `target_link_libraries`（能精确表达 PRIVATE，CMake 老手推荐）

### 5.3 为什么会有 `ament_export_dependencies`

这是 ament 和标准 CMake 差别最大、也最容易踩坑的一点。

```cmake
# demo_math/CMakeLists.txt
ament_target_dependencies(demo_math PUBLIC rclcpp)   # 我依赖 rclcpp
ament_export_dependencies(rclcpp)                    # ★ 我的下游也要能找到 rclcpp
```

**标准 CMake 的世界里，依赖传递是自动的**：你 `PUBLIC` 链接了谁，
下游自动继承（因为都写在 target 属性里）。

**ament 的世界里，这个传递是半自动的**：

| 传什么 | 怎么传 | 自动吗 |
|---|---|---|
| target 之间的链接关系 | `install(EXPORT)` 写进 `Targets.cmake` | ✓ 自动 |
| 下游要触发哪个 `find_package` | `ament_export_dependencies` | ✗ **必须手写** |

漏了会报：

```
The link interface of target "demo_math::demo_math" contains:
    rclcpp::rclcpp
but the target was not found.
```

**记忆方法：凡是出现在你公开头文件里的第三方类型，都要 `export_dependencies`。**
只出现在 `.cpp` 里的，`PRIVATE` 链接即可，不用导出。

### 5.4 `HAS_LIBRARY_TARGET` 是干什么的

```cmake
ament_export_targets(demo_mathTargets HAS_LIBRARY_TARGET)
```

`demo_math` 是共享库，装在 `install/demo_math/lib/libdemo_math.so`。
运行时动态链接器默认只看 `/usr/lib` 这类系统目录，**不知道你的工作空间在哪**，
于是程序启动时报：

```
error while loading shared libraries: libdemo_math.so: cannot open shared object file
```

加上 `HAS_LIBRARY_TARGET`，ament 会给这个包生成一个**环境钩子**，
`source install/setup.bash` 时自动把库目录加进 `LD_LIBRARY_PATH`：

```bash
cat /ws/install/demo_math/share/demo_math/hook/ld_library_path_lib.dsv
# prepend-non-duplicate;LD_LIBRARY_PATH;lib
```

`.dsv` 是 ament 环境钩子的格式：`<操作>;<环境变量名>;<相对路径>`
（第三段为空表示前缀本身）。这是 `ament_environment_hooks` 提供的机制。

**想亲眼验证这个钩子真的生效**，可以做个实验：

```bash
# 1. 先 source，看 LD_LIBRARY_PATH 里有没有 demo_math
source install/setup.bash
echo $LD_LIBRARY_PATH | tr ':' '\n' | grep demo_math

# 2. 再把它清掉，看程序是不是就起不来了
export LD_LIBRARY_PATH=$(echo $LD_LIBRARY_PATH | tr ':' '\n' | grep -v demo_math | paste -sd:)
ros2 run demo_nodes controller_node
# → error while loading shared libraries: libdemo_math.so
```

这个实验能让你彻底分清**编译期依赖**（`-I` 和 `-l`）和**运行期依赖**（`LD_LIBRARY_PATH`）——
它们是两件完全不同的事，ROS2 的 `HAS_LIBRARY_TARGET` 就是在补后一半。

**如果 `add_library` 用的是 `STATIC`，就不需要这个选项** ——
静态库代码被直接拷进可执行文件，运行时不用找。

---

## 6. ament_cmake 有必要学吗？

**必须学。而且它比你想的小得多。**

### 6.1 为什么必须学

1. **它不是可选项。** ROS2 里 C++ 包只有三种 `build_type`：

   | build_type | 用什么构建 | 什么时候用 |
   |---|---|---|
   | `ament_cmake` | CMakeLists.txt | C++ 包，**99% 的 ROS2 C++ 代码**  |
   | `ament_python` | setup.py | 纯 Python 包 |
   | `cmake` | CMakeLists.txt | 不想装 ament 的纯 CMake 包（很罕见） |

   你写 ROS2 C++ 节点，就是在写 `ament_cmake` 包。没有绕过的办法。

2. **不学它，你会在报错时完全失去判断力。**
   看到 `ament_export_dependencies` 漏了导致的 `target was not found`，
   不知道这个宏存在的人根本无从下手 —— 因为报错信息里
   只提到 CMake 的 target，完全没提 ament。

3. **它决定了你的包能不能被别人用。**
   `ament_export_targets` / `ament_export_dependencies` 这两行，
   是"我的库能不能被下游链接"的唯一开关。写错一个字母，
   下游永远找不到你的 target。

### 6.2 为什么它其实不难

因为**它不是新语言，也不是新构建系统**。整个 `ament_cmake` 的核心 API
就是 5.1 节那张表 —— **十来个宏**，一两个小时能全部掌握。

而且每个宏展开后都是标准 CMake 命令。理解了 target / PUBLIC / PRIVATE，
ament 就只剩下"这几个宏名字是什么"的记忆量了。

### 6.3 建议的学习顺序

```
第 1 步  CMake 基础          （target、属性、install、find_package）
           ↓ 本工程的 demo_math + demo_tools 对照着看
第 2 步  CMake 的 export 机制（install(EXPORT)、Config.cmake 是什么）
           ↓ 去 install/*/share/*/cmake/ 里读生成的文件
第 3 步  ament 的 3 个导出宏  （export_targets / export_dependencies / package）
           ↓
第 4 步  rosidl 代码生成      （最后学，因为它最"魔法"）
```

**不建议**从 `ament_cmake_auto` 开始。它用 `ament_auto_find_build_dependencies()`
之类的宏把 `find_package` 都藏起来了，写起来短，但你会完全不知道
CMake 在干什么 —— 出问题时无从下手。**先用显式写法把机制搞清楚。**

---

## 7. interface 三兄弟：msg / srv / action

前面 §2.5 已经从**构建产物**的角度证明了 action 是拼出来的。
这一章从**怎么选、怎么写、CMake 怎么配**的角度把三者对齐。

### 7.1 三者的关系，一句话

```
                底层通信原语（只有这两种！）
                ┌──────────┐   ┌───────────┐
                │  topic   │   │  service  │
                └────┬─────┘   └─────┬─────┘
                     │               │
      ┌──────────────┘               │
      │                              │
      ▼                              ▼
   msg/Target                  srv/Reset
   （1 个话题）                 （1 个服务）

                     action/Calibrate
              ┌──────────────────────────────┐
              │ = 3 个 service + 2 个 topic   │
              │   ├── send_goal   service     │
              │   ├── get_result  service     │
              │   ├── cancel_goal service ────┼──► 借用 action_msgs/CancelGoal
              │   ├── feedback    topic       │
              │   └── status      topic ──────┼──► 借用 action_msgs/GoalStatusArray
              └──────────────────────────────┘
```

**msg 和 srv 是"一等公民"，action 是"用一等公民搭出来的复合结构"。**
记住这张图，后面所有关于 action 的困惑都能自己推出来。

### 7.2 什么时候用哪个

| 问题 | topic | service | action |
|---|---|---|---|
| 谁 → 谁 | 1 对多 | 1 对 1 | 1 对 1（但可以多个 goal 并存） |
| 要等结果吗 | 不等，发完就完 | **等**，拿到响应才返回 | 不等，回调驱动 |
| 会跑很久吗 | —— | ✗ 必须立刻返回 | ✓ 天生为长任务设计 |
| 中途能看进度吗 | 只能靠另一个 topic | ✗ | ✓ Feedback |
| 中途能叫停吗 | —— | ✗ | ✓ Cancel |
| 典型例子 | 传感器数据、状态广播 | 读/写一个参数、查一次状态 | 导航到某点、抓取、**校准** |

判断链（三句话，都答"否"就选 service）：

```
这个东西会跑很久吗？      ── 是 ──┐
中途想看到进度吗？        ── 是 ──┼──► action
中途可能要取消吗？        ── 是 ──┘
三个都否 ──► service
不需要应答、只要持续广播 ──► topic
```

> ★ 一个高频误判：**"耗时长"不是 action 的充分条件，只是必要条件。**
> 如果一次数据库查询要 3 秒，但你只需要那 3 秒后的一个结果，
> 那还是 service —— 因为它不需要反馈、不需要取消。
> 反过来说，一个只跑 1 秒的任务如果是"移动机械臂到某处"，
> 那它就该是 action（因为中途可能出事儿要叫停）。

### 7.3 三者在 CMake / package.xml 里的对照

这是本章的**实用部分**。三种接口在构建系统里的写法，逐项对照：

| 要做的事 | msg | srv | action |
|---|---|---|---|
| 源文件放哪 | `msg/X.msg` | `srv/Y.srv` | `action/Z.action` |
| CMake 里怎么注册 | `rosidl_generate_interfaces(${PROJECT_NAME} "msg/X.msg" ...)` —— **三种写在一起，同一个宏** | | |
| 需要哪个 find_package | `std_msgs`（若引用 Header） | 无额外 | `action_msgs` —— 写也行，**宏会自己补**（见 §7.4） |
| package.xml 要不要额外依赖 | 看引用 | 看引用 | **要，`<depend>action_msgs</depend>`**（这条才是真必须的） |
| 要不要 `rosidl_default_generators` | 要 | 要 | 要（同一条） |
| 要不要 `member_of_group` | **要** | 要 | 要（同一条） |
| 用它的节点要 find_package 什么 | `rclcpp` | `rclcpp` | **`rclcpp_action`**（额外） |
| 用它的节点要 include 什么 | `demo_interfaces/msg/x.hpp` | `demo_interfaces/srv/y.hpp` | `demo_interfaces/action/z.hpp` + `rclcpp_action/rclcpp_action.hpp` |
| CLI 看定义 | `ros2 interface show demo_interfaces/msg/X` | `.../srv/Y` | `.../action/Z` |
| CLI 调用 | `ros2 topic echo/pub` | `ros2 service call` | `ros2 action send_goal ... --feedback` / `ros2 action list` |

**只有两处是 action 独有的**，就是上面加粗的那两行 —— 而且它们都是"多一个依赖"，
不是什么新机制。**其余每一行，三种接口走的是同一条路。**

### 7.4 最容易搞混的一对：`action_msgs` vs `rclcpp_action`

这一对是 action 相关 CMake 报错的头号来源，单独拎出来说：

```
                 定义接口的一侧                使用接口的一侧
              （demo_interfaces 包）        （demo_nodes 包）

  源文件        action/Calibrate.action       controller_node.cpp
                                              tune_client_node.cpp
  要 find 谁    action_msgs                   rclcpp_action
  为什么        .action 展开出的服务/消息       create_server() / create_client()
                里嵌了 action_msgs 的类型      这些函数在这个库里
  写在哪        demo_interfaces/CMakeLists    demo_nodes/CMakeLists

  ★ 记住这条分界线：**定义接口 vs 使用接口。**
```

对应到本工程，四个文件里各有一行：

```cmake
# src/demo_interfaces/CMakeLists.txt —— 定义侧
find_package(action_msgs REQUIRED)
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/Target.msg" "srv/Reset.srv" "action/Calibrate.action"
  DEPENDENCIES std_msgs action_msgs)      # ← 给代码生成器看

# src/demo_nodes/CMakeLists.txt —— 使用侧
find_package(rclcpp_action REQUIRED)       # ← 给写 server/client 用
ament_target_dependencies(controller_node rclcpp demo_interfaces demo_tools rclcpp_action)
```

```xml
<!-- src/demo_interfaces/package.xml -->
<depend>action_msgs</depend>

<!-- src/demo_nodes/package.xml -->
<depend>rclcpp_action</depend>
```

**写错的症状 —— 注意这里有个实测出来的反直觉事实（见 §8）：**

| 写错的地方 | 报错 |
|---|---|
| `demo_nodes` 漏了 `rclcpp_action` | 编译期：`rclcpp_action/rclcpp_action.hpp: No such file`（实测） |
| `demo_interfaces` 漏了 `find_package(action_msgs)` | **什么都不发生** ← 见下面的实测 |
| `demo_interfaces` 的 `DEPENDENCIES` 漏了 `action_msgs` | **什么都不发生**（同上） |
| `demo_interfaces/package.xml` 漏了 `<depend>action_msgs</depend>` | 本机不报错，但 `rosdep` / 干净环境会出问题 |
| 把 `rclcpp_action` 写进 `demo_interfaces` | 能编过，但**毫无意义** —— 消息包不写节点，用不上它 |

#### 实测：`rosidl_generate_interfaces()` 会**自己**去拿 `action_msgs`

把 `demo_interfaces/CMakeLists.txt` 里的 `find_package(action_msgs REQUIRED)`
注释掉、`DEPENDENCIES` 里的 `action_msgs` 删掉，**两个一起删**，重新编译：

```
Finished <<< demo_interfaces [3.46s]
Summary: 1 package finished
```

**照样过。** 因为 Humble 的
`/opt/ros/humble/share/rosidl_cmake/cmake/rosidl_generate_interfaces.cmake`
里有这么一段（第 140–156 行）：

```cmake
# to generate action interfaces, we need to depend on "action_msgs"
foreach(_tuple ${_interface_tuples})
  ...
  if("${_parent_dir}" STREQUAL "action")          # ← 只要有一个文件在 action/ 目录下
    find_package(action_msgs QUIET)                # ← 宏自己 find
    if(NOT ${action_msgs_FOUND})
      message(FATAL_ERROR
        "Unable to generate action interface for '${_tuple_file}'. "
        "In order to generate action interfaces you must add a depend tag "
        "for 'action_msgs' in your package.xml.")   # ← 注意它让你改的是 package.xml
    endif()
    list_append_unique(_ARG_DEPENDENCIES "action_msgs")   # ← 宏自己加进 DEPENDENCIES
    ament_export_dependencies(action_msgs)               # ← 宏自己导出
    break()
  endif()
endforeach()
```

**能读出三件事**（前两条是实测，第三条是机制推理，我标明一下）：

1. **实测**：`.action` 一出现，`action_msgs` 就被自动接上了 ——
   `find_package`、`DEPENDENCIES`、`ament_export_dependencies` 三件事，宏全包了。
   所以本工程里手写的那两行**是冗余的**（写出来只是为了让"依赖"这件事
   在代码里看得见，教学上更清楚）。
2. **实测**：`DEPENDENCIES` 里写了但没 `find_package` 的包会直接炸（见下面），
   而 `action_msgs` 属于例外 —— **宏自己补的那一份，绕不过去，也不会漏**。
3. **推论**（我在这台机器上没能验证，因为没装 `rosdep`）：
   那个 `FATAL_ERROR` 什么时候才触发？只有当
   `find_package(action_msgs QUIET)` **真的找不到**它的时候。
   而 `package.xml` 决定 colcon 把哪些包放进构建环境 ——
   在一个"只装了被依赖包"的干净环境里，"没声明 = 没装 = 找不到"。
   本机 `/opt/ros/humble` 里恰好什么都有，所以把这一切掩盖了。

> ★ **这一小节的通用教训**（比 action 本身更值得记）：
> **"我这儿能编"永远不是"依赖写对了"的证据。**
> 想知道自己有没有漏声明，要问的是**依赖检查工具**，不是编译器。
> ROS2 的标准答案是 `rosdep`：
>
> ```bash
> sudo apt install python3-rosdep          # 本机没有装
> rosdep check --from-paths src --ignore-src
> ```
>
> 它才是在模拟"别人拿到这个包"的场景。
> （本工程这台机器上没装 `rosdep`，所以这条命令我**没有实测过** ——
> 拿它当"该怎么做"的指引，别当"跑出来长这样"的证据。）

**顺带一个真会炸的地方**（实测）：`DEPENDENCIES` 里写了但**没** `find_package` 的包，
`rosidl_generate_interfaces()` 会在第 162 行直接 `FATAL_ERROR`：

```
CMake Error at .../rosidl_generate_interfaces.cmake:162 (message):
  rosidl_generate_interfaces() the passed dependency 'std_msgs' has not been
  found before using find_package()
```

`std_msgs` / `action_msgs` 之外，**你自己加的任何 `DEPENDENCIES` 都适用这条规则** ——
这一条才是"生成阶段"最常见的报错。

### 7.5 action 的"状态机"心智模型

用 action 最别扭的地方，是从 service 的"函数调用"思维切不过来。

```
   service 的思维                       action 的思维
   ──────────────                       ──────────────

   resp = call(req)                     客户端                        服务端
   └─ 一行代码，拿到结果                   │                             │
                                         │  SendGoal 服务 ────────────►│
                                         │◄──── 接受 / 拒绝 ───────────│  handleGoal()
   中间发生什么？不知道。                    │                             │
   有没有超时？自己想办法。                   │  ◄─── feedback 话题 ────────│  每拍发进度
                                         │                             │
                                         │  CancelGoal 服务 ──────────►│  handleCancel()
                                         │◄──── 让不让取消 ────────────│
                                         │                             │
                                         │  GetResult 服务 ◄──────────►│  succeed()/abort()
                                         │◄──── 最终结果 ──────────────│     /canceled()
```

**service 是"一次函数调用"，action 是"一个有生命周期的任务对象"。**
这个差别落到代码上就是：

| service 服务端 | action 服务端 |
|---|---|
| 一个回调函数，收请求、填响应、返回 | 三个回调（goal/cancel/accepted）**都只做决策**，真正的执行要挂在别的地方 |
| 不用存状态 | **必须把任务存成状态**（本工程是 `goal_handle_` 那组成员） |
| 天然不关心并发 | 要自己决定"允不允许同时跑两个目标" |

本工程的 `controller_node` 就是照着这个写的：
`handleGoal` / `handleCancel` / `handleAccepted` 三个回调全部立刻返回，
真正的推进放在控制定时器里（`updateCalibration()`），
**整个任务状态永远只有一个线程在改，不用加锁也不会打架。**

```cpp
// 三个回调都只做决策，不做收尾 —— 这是 action 服务端的标准写法
rclcpp_action::GoalResponse   handleGoal(...);    // 接不接？
rclcpp_action::CancelResponse handleCancel(...);  // 让不让取消？
void                          handleAccepted(...); // 记下来，等下一拍推进

// 唯一的"推进点"，和已存在的控制循环复用同一个线程
void updateCalibration(...);  // 取消 / 成功 / 超时 / 发反馈，四个分支
```

> ★ 一个具体的坑：如果你的定时器周期是 20ms，而取消回调里直接调 `canceled()`，
> 那就等于在回调线程里操作服务端内部状态。单线程 executor 下通常能跑，
> 一旦换成多线程 executor 就会偶发死锁。
> **把状态修改收敛到一个地方**，是这个坑唯一的通用解法。

### 7.6 本工程里三种接口的"一次完整交互"

把三个文件对照着扫一遍，比读十页文档快：

| | topic | service | action |
|---|---|---|---|
| 定义 | [Target.msg](src/demo_interfaces/msg/Target.msg) | [Reset.srv](src/demo_interfaces/srv/Reset.srv) | [Calibrate.action](src/demo_interfaces/action/Calibrate.action) |
| 服务端/发布端 | [controller_node.cpp](src/demo_nodes/src/controller_node.cpp) `onTimer()` | 同文件 `onReset()` | 同文件 `handleGoal/handleCancel/handleAccepted/updateCalibration` |
| 客户端/订阅端 | [monitor_node.cpp](src/demo_nodes/src/monitor_node.cpp) | `ros2 service call` | [tune_client_node.cpp](src/demo_nodes/src/tune_client_node.cpp) |
| 退出码/失败语义 | 无 | `success` 字段（自己定） | `ResultCode`：`SUCCEEDED`/`ABORTED`/`CANCELED` |

---

## 8. 报错对照表

| 报错 | 根因 | 修法 |
|---|---|---|
| `fatal error: demo_math/pid.hpp: No such file` | include 目录没传过来 | 检查上游是不是写成了 PRIVATE；检查 `target_include_directories` 的 `$<INSTALL_INTERFACE:...>` |
| `undefined reference to 'demo_math::Pid::update'` | 编过了但没链接 | `target_link_libraries` / `ament_target_dependencies` 漏了 |
| `Target "controller_node" links to target "demo_math::demo_math" but the target was not found. Perhaps a find_package() call is missing` | 上游 `ament_export_dependencies` 少了这个包，下游没 `find_package` 它 | 在上游补上 `ament_export_dependencies(demo_math)` |
| `install(EXPORT "X" ...) includes target "A" which requires target "B" that is not in any export set` | 静态库 PRIVATE 链接了一个没导出的 target | 见练习 d |
| `Package 'demo_tools' not found` | 没 `source install/setup.bash`，或包没 build 成功 | 先 source；确认 `install/demo_tools/` 存在 |
| `ros2 run` 报 `No executable found` | 装到 `bin/` 去了 | `DESTINATION` 改成 `lib/${PROJECT_NAME}` |
| `cannot open shared object file: libdemo_math.so` | 共享库运行时找不到 | `ament_export_targets(... HAS_LIBRARY_TARGET)` |
| `relocation R_X86_64_32 ... recompile with -fPIC` | 静态库进共享库没开 PIC | `set_target_properties(t PROPERTIES POSITION_INDEPENDENT_CODE ON)` |
| `message type not found` / 自定义消息编不过 | `package.xml` 少了 `<member_of_group>rosidl_interface_packages</member_of_group>` | 补上 |
| 改了 launch / yaml 不生效 | 它们被 install 到 `share/` 下，源码目录的改动没同步 | 重新 `colcon build` |
| 参数写了但节点用的是默认值 | `params.yaml` 顶层 key 和 launch 里的 `name=` 对不上 | 两边改成一致 |
| `rosidl_generate_interfaces() the passed dependency 'std_msgs' has not been found before using find_package()` | `DEPENDENCIES` 里写了某个包，但上面没有 `find_package()` 它 | 补上 `find_package(std_msgs REQUIRED)`；两处必须配套（实测，见 §7.4） |
| `Unable to generate action interface for '...'. ... you must add a depend tag for 'action_msgs' in your package.xml` | 有 `.action` 文件但**找不到** `action_msgs` 包 | 这个报错本身就告诉你了：在 `package.xml` 加 `<depend>action_msgs</depend>`（而在本机 `/opt/ros/humble` 里它总是找得到，所以通常看不到这条） |
| 在 `demo_interfaces` 里删掉 `find_package(action_msgs)` 和 `DEPENDENCIES` 里的 `action_msgs`，**却什么都没发生** | 别以为漏写了 —— `rosidl_generate_interfaces()` 会自动替你 `find_package` + 加依赖 + 导出 | 不是 bug，见 §7.4 的宏源码；手写只是为了可读性 |
| `fatal error: rclcpp_action/rclcpp_action.hpp: No such file` | `ament_target_dependencies` 里漏了 `rclcpp_action`（它的 include 目录是**按包**注册的，不会跟着 `rclcpp` 一起来） | 补上 `find_package(rclcpp_action REQUIRED)` 和 `ament_target_dependencies(... rclcpp_action)`（实测见练习 k） |
| `undefined reference to rclcpp_action::create_server<...>` | 头文件可见但库没链接（比如手写 `target_include_directories` 却忘了 `target_link_libraries(... rclcpp_action::rclcpp_action)`） | 用 `ament_target_dependencies` 一把配齐，或手动把 target 链上 |
| `package.xml` 里漏了 `<depend>rclcpp_action</depend>`，但本机照样编过 | **`package.xml` 不参与本机编译**，只在 `rosdep` / 构建顺序计算时起作用 | 别拿"我这儿能编"当依赖写对了的证据，见练习 k |
| `rosidl_generate_interfaces() must be called before ...` / action 类型生成了但代码里找不到 | 文件放错目录，或者忘了在宏里列出 | `.action` 必须在 `action/` 目录下，且必须出现在 `rosidl_generate_interfaces()` 的参数里（见 §2.5） |
| 客户端 `wait_for_action_server()` 超时 | 服务端没起 / 名字对不上 / 起来太慢 | 先起 `controller_node`；确认两边名字一致（`calibrate`）；超时给足（DDS 发现要几秒） |
| 客户端发了 goal 但服务端毫无反应 | 服务端 `handleAccepted` 里存了 handle 却**忘了启动执行** | 本工程把执行放在已有定时器里，所以容易漏的是"没调 `updateCalibration()`" |
| 服务端日志显示任务一直不结束 | `succeed()` / `abort()` 只在**某一拍**判断，条件永远不满足 | 检查退出条件（如 `tolerance` 比被控对象能到达的精度还小 —— 见 §1.4 的实测值） |
| 取消请求发了，客户端却一直等不到结果 | `handleCancel` 里直接调了 `canceled()` 做收尾 | `handleCancel` 只**决定**让不让取消，收尾放在定时器里（本工程 `updateCalibration` 的 `is_canceling()` 分支） |
| `ros2 action list` 看得到，`ros2 service list` 看不到 `_send_goal` | 隐藏实体默认不显示 | `ros2 service list --include-hidden-services`（见 §2.5） |
| 以为自己写了 action 就是"引入了一种新通信方式" | 误解，见 §2.5 实测 | action = 3 服务 + 2 话题，底层没有新原语 |

**从 Docker 换到 Ubuntu 原生，会新遇到的一批：**

| 报错 | 根因 | 修法 |
|---|---|---|
| `error while loading shared libraries: libdemo_math.so` / `Package 'demo_nodes' not found` | **忘了 `source install/setup.bash`**。容器里 `run.ps1` 替你做过，裸机上没人替你做了 | 每个**新开的终端**都要 `source /opt/ros/humble/setup.bash && source install/setup.bash` |
| `ros2: command not found` | 只 source 了 `install/setup.bash`，没 source `/opt/ros/humble/setup.bash` | 两个 source 是两件事，见 §1.4 的表；顺序也不能反 |
| `/opt/ros/humble/setup.bash: line 8: AMENT_TRACE_SETUP_FILES: unbound variable` | 你的脚本开了 `set -u`，而 ROS2 的 `setup.bash` 靠"读一个可能没定义的变量"来判断状态 —— **两套约定打架** | source 前后临时 `set +u` / `set -u`（实测，见 [ubuntu/build-and-run.sh](ubuntu/build-and-run.sh) 里的 `source_ros()`） |
| `E: 无法定位软件包 ros-humble-xxx` | Ubuntu 版本和 ROS2 发行版不匹配（Humble 只配 22.04/jammy） | 查 `/etc/os-release` 的 `VERSION_CODENAME`；24.04 要装的是 jazzy |
| `fatal error: rclcpp_action/rclcpp_action.hpp: No such file`（全新机器上） | 压根没装这个包 —— 它**不在** `rclcpp` 里 | `sudo apt install ros-humble-rclcpp-action`，或跑 `bash ubuntu/setup-ros2.sh` |
| 找不到 `rosidl_generate_interfaces` 这个宏 | 没装 `rosidl_default_generators`（生成 msg/srv/action 全靠它） | `sudo apt install ros-humble-rosidl-default-generators` |
| 用 `dpkg -l` 查 colcon 查不到，但它明明能用 | colcon 可能是 **pip 装的**，那种装法没有对应的 dpkg 包 | 判断"有没有"要问 `command -v colcon`，不要问包管理器 |
| 上次跑的节点还在后台发数据，控制台一团乱 | 容器用完就没了，**宿主机上的进程会一直活着** | `pkill -x controller_node`；`ubuntu/build-and-run.sh` 开头就替你做这件事 |

### 排查 `find_package` 的第一手段：让 CMake 自己招供

凡是 `find_package` 找不到包，**别去读文档猜搜索路径**。加一个参数，
CMake 会把整个过程逐行打印出来：

```bash
cmake -DCMAKE_FIND_DEBUG_MODE=ON ...
# colcon 的话：
colcon build --packages-select demo_math --cmake-args -DCMAKE_FIND_DEBUG_MODE=ON
```

本工程实测输出（`demo_math` 在自己的 CMakeLists 里找 `ament_cmake_core`，
前缀是 `/opt/ros/humble`）：

```
find_package considered the following locations for the Config module:
    /opt/ros/humble/ament_cmake_coreConfig.cmake
    /opt/ros/humble/ament_cmake_core-config.cmake
    /opt/ros/humble/cmake/ament_cmake_coreConfig.cmake
    /opt/ros/humble/cmake/ament_cmake_core-config.cmake
    /opt/ros/humble/share/ament_cmake_core/ament_cmake_coreConfig.cmake
    /opt/ros/humble/share/ament_cmake_core/ament_cmake_core-config.cmake
    /opt/ros/humble/share/ament_cmake_core/cmake/ament_cmake_coreConfig.cmake
  The file was found at
    /opt/ros/humble/share/ament_cmake_core/cmake/ament_cmake_coreConfig.cmake
```

**这七行就是"ROS2 的包是怎么被 CMake 找到的"的完整答案。**
注意最后一行 —— ament 把文件生成到 `share/<包名>/cmake/` 下，
正是为了让标准搜索能命中它。

它还会列出**所有 prefix 的来源**，这一部分信息量更大 ——
你会发现除了 `CMAKE_PREFIX_PATH`，还有 `<包名>_DIR`、`<包名>_ROOT`、
`PATH` 环境变量（`/opt/ros/humble` 就是这么进来的）、
`CMAKE_INSTALL_PREFIX`、系统目录等等。

**两条实测得到的结论**：

1. **找到第一个就停**，所以 prefix 的先后顺序就是优先级。
   `CMAKE_PREFIX_PATH="/pa;/pb"` 且两边都有 `cc` 时，`/pa` 赢，`/pb` 根本不会被查。

2. **能识别的目录布局不止一种**。下面这些（每种单独放进一个 prefix）都能命中：

   ```
   <prefix>/cc/ccConfig.cmake
   <prefix>/cc/cmake/ccConfig.cmake
   <prefix>/lib/cmake/cc/ccConfig.cmake
   <prefix>/share/cmake/cc/ccConfig.cmake
   <prefix>/cmake/ccConfig.cmake
   <prefix>/share/cc-1.2/cmake/ccConfig.cmake     ← 目录名带版本号也行
   <prefix>/share/cc/cmake/cc-config.cmake        ← 小写 -config 命名也行
   ```

   所以那句"`<Package>Config.cmake` 或 `<package>-config.cmake`"说的是**文件名**，
   而 `share/<包名>/cmake/` 只是**ROS2 选定的那一种目录布局**，
   不是 CMake 唯一认的。ament 之所以装到那儿，是为了命中标准搜索路径。

---

## 9. 动手练习

按难度排序。**每个都建议真的做一遍。**

**a. 看产物** —— 编译后打开
`install/demo_math/share/demo_math/cmake/ament_cmake_export_targets-extras.cmake`
和 `demo_mathConfig.cmake`，把内容读一遍。所谓"导出魔法"就在这里。

**b. 破坏 PUBLIC** —— 把 `demo_tools/CMakeLists.txt` 里的
`PUBLIC demo_math::demo_math` 挪到 `PRIVATE` 那组，重新 `colcon build`。

实际报错（已实测）：

```
/expt/src/demo_tools/src/tune_cli.cpp:25:10:
    fatal error: demo_math/pid.hpp: No such file or directory
gmake[1]: *** [CMakeFiles/tune_cli.dir/all] Error 2
Failed   <<< demo_tools [4.35s, exited with code 2]
```

注意失败的是 **`tune_cli.cpp`**，也就是 `demo_tools` 自己的可执行文件 ——
因为它也 `#include <demo_math/pid.hpp>`，而 `PUBLIC` 一改成 `PRIVATE`，
include 路径就不再传给链接 `demo_tools` 的人了，**包括它自己的可执行文件**。

> `PUBLIC` / `PRIVATE` 的作用域是"**谁链接了这个 target**"，
> 不是"下游包"。同一个包内的可执行文件，对库来说也是"下游"。

**c. 破坏 export** —— 把 `demo_tools/CMakeLists.txt` 里的
`ament_export_dependencies(demo_math rclcpp)` 改成 `ament_export_dependencies(rclcpp)`
（即**去掉 demo_math**），重新 `colcon build`。

实际报错（已实测）：

```
CMake Error at CMakeLists.txt:74 (add_executable):
  Target "controller_node" links to target "demo_math::demo_math" but the
  target was not found.  Perhaps a find_package() call is missing for an
  IMPORTED target, or an ALIAS target is missing?
Failed   <<< demo_nodes
```

报错在 **`demo_nodes`**，但根因在 **`demo_tools`** —— 这就是
"依赖传递要手动声明"的代价：**改了上游，炸在下游。**

> ⚠️ 不要拿 `demo_math` 的 `ament_export_dependencies(rclcpp)` 做这个实验 ——
> 删了它**不会报错**。因为 `demo_tools` 自己也 `find_package(rclcpp)`，
> 已经把 `rclcpp::rclcpp` 定义出来了。
> 这本身也是个知识点：`find_dependency` 只在你**没有**自己 find 的时候才救你。

**d. 共享库改静态库** —— 把 `demo_tools` 的 `add_library(${PROJECT_NAME} SHARED ...)`
改成 `STATIC`，重新 `colcon build`。

实际报错（已实测）：

```
CMake Error: install(EXPORT "demo_toolsTargets" ...) includes target
"demo_tools" which requires target "demo_tools_internal" that is not in
any export set.
Failed   <<< demo_tools
```

想清楚为什么：

- **SHARED 时**：`demo_tools_internal.a` 的代码被**吸收**进 `libdemo_tools.so`，
  它不会出现在 `INTERFACE_LINK_LIBRARIES` 里（§4.2 有实测证据），
  所以没人需要去找它 → 不报错。
- **STATIC 时**：`libdemo_tools.a` 只是一堆 `.o` 的打包，**没被链接过**。
  谁链接它，谁就得同时链接 `demo_tools_internal`。于是这个 PRIVATE 依赖
  以 `$<LINK_ONLY:demo_tools_internal>` 的形式**传播**出去了 ——
  可它又不在任何导出集合里，下游根本找不到 → 报错。

**答上这题，CMake 的链接模型就通了。** 这也是为什么库作者要在
SHARED / STATIC 之间做选择时，必须考虑导出问题。

**e. 加一个节点** —— 在 `demo_nodes` 里加个只订阅不发布的 logger 节点，
用 `target_link_libraries` 而不是 `ament_target_dependencies` 来链接，
对比两种写法的可读性。

**f. 加子目录** —— 给 `demo_nodes` 加一个 `src/legacy/` 子目录 +
`add_subdirectory(src/legacy)`，在新目录的 CMakeLists 里打印
`${CMAKE_CURRENT_SOURCE_DIR}` 和 `${PROJECT_SOURCE_DIR}`，
体会两者的区别。

**g. 手写 cmake 命令** —— 绕开 colcon，手动编译一个包：

```bash
source /opt/ros/humble/setup.bash
cmake /ws/src/demo_math -B /tmp/manual_build \
      -DCMAKE_INSTALL_PREFIX=/tmp/manual_install \
      -DCMAKE_PREFIX_PATH=/ws/install/demo_interfaces
cmake --build /tmp/manual_build
cmake --install /tmp/manual_build
```

做完这个，colcon 在你眼里就彻底透明了。

**h. 不用客户端节点，纯命令行跑一次 action** ——

```bash
ros2 run demo_nodes controller_node          # 终端 1
ros2 action send_goal /calibrate demo_interfaces/action/Calibrate \
  "{target: 1.0, tolerance: 0.1, hold_s: 0.5, timeout_s: 25.0}" --feedback
```

`--feedback` 开着，你能在终端里看到和 `tune_client_node` 一模一样的进度流。
**这一步的意义**：证明 action 是**协议**，不是库。你的 C++ 客户端和
命令行工具走的是同一条协议，所以可以互换。

顺手再开一个终端 `ros2 action info /calibrate -t`，
在任务运行中看一眼 goal 的当前状态 —— 这就是 `_status` 话题的内容。

**i. 把"隐藏的 3 服务 2 话题"抓个现行** ——

```bash
ros2 run demo_nodes controller_node            # 终端 1
# 终端 2，**不用发任何目标**：
ros2 service list --include-hidden-services | grep calibrate
ros2 topic   list --include-hidden-topics   | grep calibrate
ros2 topic info /calibrate/_action/status
```

实测输出：

```
$ ros2 service list --include-hidden-services | grep calibrate
/calibrate/_action/cancel_goal
/calibrate/_action/get_result
/calibrate/_action/send_goal
$ ros2 topic list --include-hidden-topics | grep calibrate
/calibrate/_action/feedback
/calibrate/_action/status
$ ros2 topic info /calibrate/_action/status
Type: action_msgs/msg/GoalStatusArray
Publisher count: 1
Subscription count: 0
```

**★ 五个实体在节点启动那一刻就全在了**，哪怕一个 goal 都没发过、
一个订阅者都没有（`Subscription count: 0`，但发布者确实是 1）。

这件事值得停一下：**action 在网络上占的位置是固定的。**
一个 action 服务端一启动，就实打实地占用了 5 个通信实体名；
而没有客户端的时候，`_status` 那个发布者照样存在，只是在往外发空的东西。

顺手把 `_status` 的内容也看一眼（**这个必须发个目标才有内容**）：

```bash
ros2 run demo_nodes tune_client_node &      # 或者 ros2 action send_goal --feedback
sleep 2
ros2 topic echo --once /calibrate/_action/status
```

实测字段（截取）：

```yaml
status_list:
- goal_info:
    goal_id:
      uuid: [193, 251, 12, 187, ...]     # 16 字节，就是客户端生成的那个 goal_id
    stamp:
      sec: 1792052469
    status: 2                             # ← 这个整数在变
```

对着字段念一遍：`goal_info` 里有 goal_id 和**时间戳**，
`status` 是一个整数 —— 它的取值就是 `action_msgs/msg/GoalStatus`
里那几个常量（`STATUS_EXECUTING` = 2、`STATUS_SUCCEEDED` = 4、……）。
**你在 C++ 里调 `succeed()`，落到网络上就是这一个 `int8` 变了值。**

**这题做完，§2.5 那张表就从"文档"变成"你亲手看过的东西"了。**

**j. 让校准必失败** —— 把客户端的 `tolerance` 改成 `0.01`（物理上到不了）：

```bash
ros2 run demo_nodes tune_client_node --ros-args -p tolerance:=0.01 -p timeout_s:=10.0
```

观察三件事：

1. 服务端日志里走的是 `abort()` 分支，不是 `succeed()`；
2. 客户端打印的是"失败（服务端主动放弃）"（`ABORTED`）；
3. **客户端仍然收到了完整的 `Result`** —— `success=false`、`final_error` 是那个到不了的值。

> ★ 第 3 点是想让你记住：**"任务失败"和"通信失败"是两码事。**
> `ABORTED` 是一次**成功的** action 交互 —— 协议层完好无损地跑完了，
> 只是业务结论是"没做到"。action 的 `Result` 里那个 `message` 字段
> 就是留给这种话的。

**k. 破坏 action 的依赖，看它怎么炸** —— 这是本章最重要的练习。
每次只改**一处**，重新 `colcon build`，把报错抄下来，再改回去：

| 改哪 | 现象（全部实测） |
|---|---|
| `demo_interfaces/CMakeLists.txt` 去掉 `find_package(action_msgs)` | **什么都不发生** —— 宏自己补上了 |
| `rosidl_generate_interfaces` 的 `DEPENDENCIES` 去掉 `action_msgs` | **什么都不发生** —— 同上 |
| `demo_interfaces/CMakeLists.txt` 去掉 `find_package(std_msgs)`（但 `DEPENDENCIES` 里还留着它） | 配置阶段 `FATAL_ERROR`：`... the passed dependency 'std_msgs' has not been found before using find_package()` |
| `demo_nodes/CMakeLists.txt` 的 `ament_target_dependencies` 去掉 `rclcpp_action` | 编译期 `fatal error: rclcpp_action/rclcpp_action.hpp: No such file or directory` |
| `demo_nodes/package.xml` 去掉 `<depend>rclcpp_action</depend>` | **什么都不发生**，本机 `colcon build` 照样过 |

实测输出（第三行 —— 真正会炸的那条）：

```
CMake Error at /opt/ros/humble/share/rosidl_cmake/cmake/rosidl_generate_interfaces.cmake:162 (message):
rosidl_generate_interfaces() the passed dependency 'std_msgs' has not been
found before using find_package()
Failed   <<< demo_interfaces [0.83s, exited with code 2]
```

第四行的实测输出：

```
controller_node.cpp:36:10: fatal error: rclcpp_action/rclcpp_action.hpp:
    No such file or directory
Failed   <<< demo_nodes [1.47s, exited with code 2]
```

★ **前两行最值得琢磨。** 按直觉，"漏了个依赖"怎么也该报错吧？
偏偏不报 —— 因为 `rosidl_generate_interfaces()` 自己在第 140–156 行
补上了 `action_msgs`（源码见 §7.4）。
**这份"体贴"正是很多人的认知盲区**：他以为依赖是自己在管，
其实一部分是宏在管，于是下次换个不是 `action_msgs` 的依赖就翻车了。

★ 而第四行（`rclcpp_action`）报的是**编译期**错，也值得停下来想：

- 头文件装在 `/opt/ros/humble/include/rclcpp_action/`，
  它**和 `rclcpp` 不是同一个 include 目录**。
  `ament_target_dependencies` 是**按包**注册 include 目录的 ——
  漏了 `rclcpp_action`，编译器就看不到那个目录，
  于是报"头文件找不到"，而**不是**"链接不上"。
- 想要链接期的 `undefined reference to rclcpp_action::...`，
  得让头文件可见但库不可见。造法：把 `ament_target_dependencies`
  换成手写 `find_package(rclcpp_action REQUIRED)` +
  `target_include_directories(... ${rclcpp_action_INCLUDE_DIRS})`，
  但**不**链接 `rclcpp_action::rclcpp_action`。

★ 第五行才是真正的"教训行"：

**`package.xml` 完全不影响你本机编译。** 它影响的是
"别人 `rosdep install` 时能不能把依赖装上"、
"`colcon build` 能不能算出正确的构建顺序"。
所以**"我这儿能编"从来不是"依赖写对了"的证据** ——
本机 `/opt/ros/humble` 里恰好什么都有，掩盖了一切漏声明。

验证方法是问依赖检查工具，不是问编译器（这台机器上没装 `rosdep`，
下面第一条**没实测过**，写出来是给你在自己机器上用的）：

```bash
sudo apt install python3-rosdep
rosdep check --from-paths src --ignore-src     # 看它认不认得出依赖

colcon build --packages-select demo_nodes --cmake-clean-cache   # 这条实测过，会强制重跑 CMake
```

**l.（进阶）让 action 支持多个并发 goal** ——

现在的 `handleGoal` 在 `goal_handle_` 非空时直接拒绝。改成：

- 用一个 `std::map<GoalUUID, GoalHandle::SharedPtr>` 存所有活动目标；
- `updateCalibration` 遍历它们，每个各自判断成功/超时、各自发反馈；
- 客户端那边同时发两个目标（一个 target=2.0，一个 target=-1.0）。

你会**亲眼看到** PID 被两个目标来回撕扯，两个任务都很难收敛 ——
然后你就理解了为什么真实机器人上几乎不用并发 goal，
而是用"新目标顶掉旧目标"（`handleGoal` 里先 `cancel` 旧的再加新的）。

> 这题的价值不在于写出能用的代码，而在于**把"允许并发"这个决定
> 从"随手写的 if"变成"想过的设计选择"**。

**m. 亲手制造那个 `unbound variable`** ——

[ubuntu/build-and-run.sh](ubuntu/build-and-run.sh) 里有一段 `source_ros()`，
专门在 `source` 前后开关 `set -u`。把那个函数改坏、直接 source，你会看到：

```
/opt/ros/humble/setup.bash: line 8: AMENT_TRACE_SETUP_FILES: unbound variable
```

然后**去读 `/opt/ros/humble/setup.bash` 的第 8 行附近**，
看它到底在干什么。你会看到类似 `if [ -n "${AMENT_TRACE_SETUP_FILES:-}" ]`
或者干脆就是裸读一个变量 —— 原来 ROS2 判断"有没有"的方式就是"读一下看看"。

> 这题真正想让你记住的是：**`set -u` 和"靠读变量探测状态"这种写法是冲突的。**
> 你会越来越多地在别人的脚本里见到这两种风格打架，
> 认出它比背下解法更值钱。

**n. 走一遍"从 Docker 换到裸机"的完整对照** ——

如果你手上两台机器（或者同一台装过 Docker）都有，把这四条各跑一遍，
对比输出：

```bash
# 容器里
bash /ws/docker/build-and-run.sh
# 宿主机
bash ubuntu/build-and-run.sh
```

重点看**差异**，别看相同的地方：

| | 容器 | 裸机 |
|---|---|---|
| 仓库路径 | 永远是 `/ws` | 取决于 clone 到哪儿 |
| 要不要先 source ROS2 | 不用（`run.ps1` 做了） | **要** |
| `chmod +x` 可执行文件 | 要（Windows 挂载会丢权限位） | 不用 |
| 上次的节点会不会残留 | 不会（容器一关就没了） | **会**，得自己 `pkill` |
| 编译速度 | 慢一些 | 快 |

> **换个环境重新学一遍，比第一次学两遍收获更大。**
> 因为这一次你知道了"哪些是 ROS2/CMake 本身的规则，
> 哪些只是 Docker 那一层的习惯" —— 上面这张表就是在做这个切分。

---

## 附：文件清单

```
.
├── README.md                       ← 你正在看的
├── src/
│   ├── demo_interfaces/            ← ① 消息包
│   │   ├── CMakeLists.txt          ←   rosidl 代码生成 + ament_export_dependencies
│   │   ├── package.xml             ←   build_depend / exec_depend 的分工
│   │   ├── msg/Target.msg
│   │   ├── srv/Reset.srv
│   │   └── action/Calibrate.action ←   三段式：Goal / Result / Feedback
│   │
│   ├── demo_math/                  ← ② 库包（用 ament 宏写）
│   │   ├── CMakeLists.txt          ←   ★ 重点: ament_export_targets
│   │   ├── package.xml             ←   <depend> vs <build_depend>
│   │   ├── include/demo_math/pid.hpp
│   │   └── src/pid.cpp
│   │
│   ├── demo_tools/                 ← ③ 库 + 工具（用纯 CMake 写）
│   │   ├── CMakeLists.txt          ←   ★ 重点: PUBLIC / PRIVATE 传递性
│   │   ├── package.xml
│   │   ├── include/demo_tools/auto_tuner.hpp
│   │   ├── src/auto_tuner.cpp
│   │   ├── src/internal/           ←   内部实现，不 install
│   │   │   ├── ring_buffer.hpp
│   │   │   └── ring_buffer.cpp
│   │   └── src/tune_cli.cpp        ←   不是 ROS 节点的可执行文件
│   │
│   └── demo_nodes/                 ← ④ 节点包
│       ├── CMakeLists.txt          ←   ★ 重点: 故意省略 find_package(demo_math)
│       │                               + find_package(rclcpp_action)
│       ├── package.xml             ←   ★ 重点: 有 demo_math，和上面形成对比
│       ├── src/controller_node.cpp ←   topic 发布 + service 服务端 + action 服务端
│       ├── src/monitor_node.cpp
│       ├── src/tune_client_node.cpp ←  action 客户端（进度 / 取消 / 结果三回调）
│       ├── launch/demo.launch.py   ←   with_client:=true 时连客户端一起起
│       └── config/params.yaml
├── ubuntu/                         ← Ubuntu 原生路线（§1.0 的两条路之一）
│   ├── setup-ros2.sh               ←   检测 + 装齐 ROS2 依赖（幂等，支持 --check-only）
│   └── build-and-run.sh            ←   一键编译 + 全套验证（不写死路径，自己算仓库根）
└── docker/                         ← Windows / 容器路线（另一条）
    ├── run.ps1                     ←   一键进容器
    └── build-and-run.sh            ←   一键编译 + 全套验证（路径写死 /ws）
```
