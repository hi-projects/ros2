# ROS2 × CMake 实战教程

一个能直接跑的 ROS2 Humble 工程，专门用来把 **ROS2 与 CMake 的关系**讲透。

不是"照着敲一遍能跑"的 demo —— 每个 `CMakeLists.txt` 都当教材写，
逐行注释了"这条命令在干什么、为什么必须这么写、写错了会报什么错"。

---

## 0. 这个例子是什么

一个 PID 闭环控制 demo。控制器的参数由另一个包里的库提供，
消息类型由第三个包生成，还有一个纯命令行工具用来离线整定参数。

```
                    demo_interfaces          自定义 msg / srv
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
    demo_nodes ───────► 两个 ROS 节点            ．只产生可执行文件
    （节点包）            controller + monitor    ．学: install 可执行文件与资源
```

四个包刚好覆盖四种典型情况：

| 包 | 产出 | 学到的 CMake 机制 |
|---|---|---|
| `demo_interfaces` | 头文件（由 `.msg`/`.srv` 生成） | 代码生成如何挂到 CMake 上 |
| `demo_math` | 共享库 `libdemo_math.so` | 怎么把库**导出**给别人用 |
| `demo_tools` | 共享库 + 可执行文件 | 怎么**消费**别人的库；PUBLIC/PRIVATE |
| `demo_nodes` | 两个可执行文件 | install 规则、依赖传递的终点 |

---

## 1. 跑起来

### 1.1 环境

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

### 1.2 进入容器

在仓库根目录：

```powershell
powershell -ExecutionPolicy Bypass -File .\docker\run.ps1
```

它会挂载当前目录到容器里的 `/ws`，source 好 ROS2 环境，然后给你一个 shell。

### 1.3 编译

```bash
cd /ws
colcon build
source install/setup.bash
```

> `source install/setup.bash` 这一步新手最容易漏。见 [3.3](#33-find_package-凭什么能找到你自己写的包)。

### 1.4 三个能跑的东西

```bash
# ① 离线整定工具 —— 不是 ROS 节点，不需要起 DDS，最快能看到结果
ros2 run demo_tools tune_cli
#   也可以绕开 ros2 CLI 直接执行（它就是个普通可执行文件）：
#   ./install/demo_tools/lib/demo_tools/tune_cli

# ② 两个节点一起跑，看 PID 闭环跟踪
ros2 launch demo_nodes demo.launch.py

# ③ 运行时改设定值，观察控制器重新收敛
ros2 service call /controller/reset demo_interfaces/srv/Reset "{new_setpoint: 2.0}"
```

> 注意 `tune_cli` 属于 **demo_tools** 包，不是 demo_nodes。
> `ros2 run <包名> <可执行名>` 的第一个参数是**包名**，它去 `lib/<包名>/` 下找可执行文件。
> 包名写错会报 `No executable found`。

也可以一条命令跑完全套检查：

```bash
bash /ws/docker/build-and-run.sh
```

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
```

**`.msg` 文件在这里被真正"翻译"成了 C++ 结构体。**
`rosidl_generate_interfaces()` 背后做的事，就是这一堆代码生成器 + 编译。

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

## 7. 报错对照表

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

## 8. 动手练习

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
│   │   └── srv/Reset.srv
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
│       ├── package.xml             ←   ★ 重点: 有 demo_math，和上面形成对比
│       ├── src/controller_node.cpp
│       ├── src/monitor_node.cpp
│       ├── launch/demo.launch.py
│       └── config/params.yaml
└── docker/
    ├── run.ps1                     ← 一键进容器
    └── build-and-run.sh            ← 一键编译 + 全套验证
```
