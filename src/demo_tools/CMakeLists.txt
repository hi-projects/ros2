# =============================================================================
#  demo_tools —— 用**纯标准 CMake 命令**写的 ament 包
#
#  和 demo_math/CMakeLists.txt 对照着看。两个文件干的事情几乎一样，
#  但这一个几乎不用 ament_* 宏，全部用标准 CMake 命令。
#
#  目的就是让你亲眼确认那句话：
#
#      ament_cmake 不是另一个构建系统，它就是 CMake + 一层薄宏。
#
#  ament 宏提供的"便利"，标准 CMake 命令全都能做到，而且表达能力更强
#  （比如 PRIVATE 链接 —— 老式 ament 宏根本表达不了）。
# =============================================================================

cmake_minimum_required(VERSION 3.8)
project(demo_tools)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(ament_cmake REQUIRED)

# -----------------------------------------------------------------------------
# 【1】find_package 一个"自己的包"，和找一个系统包写法完全一样
#
# 这就是 colcon 的核心价值：它把工作空间里所有包 install 到同一个前缀下，
# 并把那个前缀加进 CMAKE_PREFIX_PATH，于是 demo_math 和 rclcpp 对 CMake
# 来说没有任何区别 —— 都是"在某个前缀下能找到的包"。
#
# ★ 这里写 find_package(demo_math) 的前提，是 demo_math 的 CMakeLists 里
#   写了 ament_export_targets + ament_package。少了任何一步，这里就找不到。
#   反过来，只要你按 demo_math 的模板写，你的包就能被别人这么用。
# -----------------------------------------------------------------------------
find_package(rclcpp REQUIRED)
find_package(demo_math REQUIRED)

# -----------------------------------------------------------------------------
# 【2】一个**不导出**的内部静态库
#
# 注意和下面 demo_tools 的对比，这是本文件最重要的部分：
#
#   demo_tools_internal : 没有 install()、没有 EXPORT、没有 ament_export_targets
#   demo_tools          : 三样都有
#
# 意味着：
#   demo_tools_internal 只在这棵构建树里存在。编译完就"消失"了，
#   它的代码被原样拷进 demo_tools.so。别的包永远无法链接它。
#
# ★ 它自己的 include 目录用 PUBLIC 是没问题的 ——
#   "PUBLIC" 的作用域仅限于**链接过它的 target**，而链接它的只有 demo_tools 自己。
#   PUBLIC / PRIVATE 不是"保密级别"，是"传播范围"。
# -----------------------------------------------------------------------------
add_library(${PROJECT_NAME}_internal STATIC
  src/internal/ring_buffer.cpp
)

# ★ 静态库要被链进共享库，必须用位置无关代码 (-fPIC)。
#   否则链接时报 "relocation R_X86_64_32 against ... can not be used when
#   making a shared object; recompile with -fPIC"。
#   共享库默认就是 PIC，静态库默认不是，所以这一行是必需的。
#   （写成 target 的属性而不是全局 CMAKE_POSITION_INDEPENDENT_CODE，
#     同样是为了不污染别人的构建。）
set_target_properties(${PROJECT_NAME}_internal PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(${PROJECT_NAME}_internal PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# -----------------------------------------------------------------------------
# 【3】对外的共享库 —— 三种依赖关系各来一个，务必看清 PUBLIC / PRIVATE
#
#   target_link_libraries(<target> <作用域> <依赖 target...>)
#
# 语义（背下来，这是现代 CMake 的全部）：
#
#   PRIVATE   <依赖>：我用它，但用我的人**不需要**知道它
#                     传播范围：只有本 target
#   PUBLIC    <依赖>：我用它，用我的人**也**会用到它
#                     传播范围：本 target + 所有下游
#   INTERFACE <依赖>：我不用它，但用我的人需要它
#                     传播范围：只有下游（纯头文件库常用，本工程没用到）
#
# 下面三行正好覆盖前两种：
#
#   PUBLIC  demo_math::demo_math
#       auto_tuner.hpp 里 #include <demo_math/pid.hpp>，构造函数收 demo_math::Pid&。
#       下游必须能看见 demo_math 的类型，所以必须传下去。
#
#   PUBLIC  rclcpp::rclcpp
#       auto_tuner.hpp 里用了 rclcpp::Duration。同理。
#
#   PRIVATE demo_tools_internal
#       ring_buffer 只出现在 auto_tuner.cpp 里（pimpl 挡住了）。
#       下游不需要、也不应该看到它，所以不传。
#
# ★ 这一行完全等价于 demo_math 里的 ament_target_dependencies(... PUBLIC rclcpp)。
#   区别只有一个：用 target_link_libraries 你能精确控制每个依赖的作用域，
#   而 ament_target_dependencies 只有 PUBLIC/INTERFACE 两档。
# -----------------------------------------------------------------------------
add_library(${PROJECT_NAME} SHARED
  src/auto_tuner.cpp
)

target_link_libraries(${PROJECT_NAME}
  PUBLIC
    demo_math::demo_math
    rclcpp::rclcpp
  PRIVATE
    ${PROJECT_NAME}_internal
)

target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

# -----------------------------------------------------------------------------
# 【4】可执行文件 —— 顺便验证"依赖会传递"是不是真的
#
# tune_cli.cpp 里 include 了 <demo_math/pid.hpp>，
# 但下面只链接了 demo_tools，**没有**写 demo_math。
#
# 它编得过，原因就是 demo_tools 用 PUBLIC 链接了 demo_math：
# 那份 include 目录顺着 target 的 INTERFACE 属性传到了这里。
#
# ★ 你可以做个实验：把上面 demo_tools 的 PUBLIC demo_math::demo_math
#   改成 PRIVATE，然后重新 colcon build。tune_cli.cpp 编译会立刻报
#   "demo_math/pid.hpp: No such file or directory"。
#   亲手做一次，你就永远记住 PUBLIC 和 PRIVATE 的区别了。
# -----------------------------------------------------------------------------
add_executable(tune_cli
  src/tune_cli.cpp
)

# 可执行文件是"终点"，没人链接它，所以 PRIVATE 就够了
target_link_libraries(tune_cli PRIVATE
  ${PROJECT_NAME}
)

# -----------------------------------------------------------------------------
# 【5】install —— 装什么、不装什么，是一个设计决策
#
#   demo_tools          -> 装 + 进 EXPORT 集合（给下游用）
#   demo_tools 的头文件 -> 装（给下游用）
#   tune_cli            -> 装（给人用 ros2 run / 直接执行）
#   demo_tools_internal -> ★ 不装，源码级消失
# -----------------------------------------------------------------------------

# 对外库：装 + 导出
install(TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}Targets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/
  DESTINATION include
)

# ★ 可执行文件单独 install，而且**不放进 EXPORT 集合**。
#   EXPORT 集合是给"要被链接的 target"用的；可执行文件不会被链接，
#   放进导出集里只会让下游莫名其妙地看到一个命令名的 target。
#
# ★ DESTINATION 用 lib/${PROJECT_NAME} 而不是 bin/，这是 ROS2 的约定：
#   所有可执行文件都装在 lib/<包名>/ 下，这样 `ros2 run <包名> <可执行名>`
#   才能定位到它。装到 bin/ 里 ros2 run 是找不到的。
install(TARGETS tune_cli
  DESTINATION lib/${PROJECT_NAME}
)

# -----------------------------------------------------------------------------
# 【6】导出 —— 和 demo_math 一模一样的两条，但内容多了一个
#
# ament_export_dependencies(demo_math rclcpp) 里为什么要有 demo_math？
#
# 因为下游 find_package(demo_tools) 之后拿到的 demo_tools::demo_tools，
# 它的 INTERFACE_LINK_LIBRARIES 里写着 demo_math::demo_math。
# 如果此时没有 find_package(demo_math) 发生过，CMake 会直接报：
#
#     The link interface of target "demo_tools::demo_tools" contains:
#         demo_math::demo_math
#     but the target was not found.
#
# ament_export_dependencies 会在生成的 demo_toolsConfig.cmake 里写下
#     find_dependency(demo_math)
#     find_dependency(rclcpp)
# 下游 find_package(demo_tools) 时这两句自动执行，问题消失。
#
# ★ 规律：**你 PUBLIC/INTERFACE 链接过的每个包，都要在这里列一遍。**
#   漏一个，下游就报一次 "target was not found"。
#   注意 demo_tools_internal 不在此列 —— 它是 PRIVATE 的，
#   根本不会出现在 INTERFACE_LINK_LIBRARIES 里，也就没人需要 find_package 它。
# -----------------------------------------------------------------------------
ament_export_targets(${PROJECT_NAME}Targets HAS_LIBRARY_TARGET)
ament_export_dependencies(demo_math rclcpp)

ament_package()
