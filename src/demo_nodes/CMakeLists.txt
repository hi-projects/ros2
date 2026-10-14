# =============================================================================
#  demo_nodes —— 可执行文件包
#
#  这是整条依赖链的最后一环，也是最简单的一环：
#
#      demo_interfaces  (代码生成)
#            |
#      demo_math        (纯计算库, 公开依赖 rclcpp)
#            |
#      demo_tools       (自己又有库 + 可执行文件)
#            |
#      demo_nodes       (← 你在这里: 只有可执行文件)
#
#  它值得单独看的地方是：**它故意少写了一个 find_package。** 见下面。
# =============================================================================

cmake_minimum_required(VERSION 3.8)
project(demo_nodes)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(demo_interfaces REQUIRED)
find_package(demo_tools REQUIRED)

# =============================================================================
#  ★★★ 注意这里没有 find_package(demo_math) ★★★
#
#  可是 controller_node.cpp 里明明有 #include <demo_math/pid.hpp>。
#  为什么能编过？因为依赖传递：
#
#   1) demo_math/CMakeLists.txt 里
#          target_include_directories(demo_math PUBLIC $<BUILD_INTERFACE:...include>)
#      PUBLIC 意味着这个 include 目录会写进 demo_math::demo_math 的
#      INTERFACE_INCLUDE_DIRECTORIES 属性。
#
#   2) demo_tools/CMakeLists.txt 里
#          target_link_libraries(demo_tools PUBLIC demo_math::demo_math)
#      PUBLIC 意味着 demo_math::demo_math 会出现在 demo_tools 的
#      INTERFACE_LINK_LIBRARIES 里，而 install(EXPORT) 会把这个属性
#      一起写进 install/demo_tools/share/demo_tools/cmake/demo_toolsTargets.cmake。
#
#   3) 这里 find_package(demo_tools) 执行那个文件，CMake 就把
#      demo_math 的头文件目录挂到了 controller_node 这个 target 上。
#
#  所以 -I 参数里自动多出了 demo_math 的 include 目录，编译器找得到头文件。
#
#  另外 demo_tools 还写了 ament_export_dependencies(demo_math)，
#  它保证 find_package(demo_tools) 时会自动执行 find_package(demo_math)，
#  于是 demo_math::demo_math 这个 imported target 真的存在（能被链接），
#  而不只是"头文件路径恰好传过来了"。
#
#  ─────────────────────────────────────────────────────────────────────────
#  ⚠ 但这不代表你应该这么写。这只是为了让你看见机制。
#
#  真实项目里请老老实实写 find_package(demo_math)。
#  理由是这条链有 3 跳，任何一环把 PUBLIC 改成 PRIVATE，这里立刻编译失败，
#  而报错信息指向的却是本文件 —— 排查成本远高于多写一行。
#
#  CMake 官方建议 "include what you use, link what you use"：
#  你代码里 include 了谁，就 find_package + link 谁，不要依赖传递。
#  =============================================================================

# -----------------------------------------------------------------------------
# 【1】两个可执行文件
#
# add_executable 的写法没有任何 ROS 特色，就是标准 CMake。
# 一个可执行文件可以有任意多个源文件，也可以加子目录（见下面的"扩展练习"）。
# -----------------------------------------------------------------------------
add_executable(controller_node src/controller_node.cpp)
# controller_node 用到了：rclcpp(节点框架)、demo_interfaces(自定义 msg/srv)、
#                         demo_tools(AutoTuner)、demo_math(通过 demo_tools 传递过来)
ament_target_dependencies(controller_node
  rclcpp
  demo_interfaces
  demo_tools
)

add_executable(monitor_node src/monitor_node.cpp)
# monitor_node 只用 rclcpp + demo_interfaces
ament_target_dependencies(monitor_node
  rclcpp
  demo_interfaces
)

# ★ 这里可以试试换成标准 CMake 写法，效果完全一样：
#       target_link_libraries(controller_node
#         PRIVATE rclcpp::rclcpp demo_interfaces::demo_interfaces demo_tools::demo_tools)
#   注意 demo_interfaces::demo_interfaces —— 消息包导出的 target 就叫这个。
#   消息在 CMake 眼里就是普通的库，这是 rosidl_generate_interfaces 的设计。

# -----------------------------------------------------------------------------
# 【2】install 可执行文件
#
# DESTINATION 是 lib/${PROJECT_NAME}，不是你直觉里的 bin/。
#
# 原因：ros2 run 的实现是去 <prefix>/lib/<package_name>/ 下面找可执行文件。
# 所以你写 DESTINATION bin 的话：
#     colcon build          -> 成功，没有任何警告
#     直接执行 ./install/demo_nodes/bin/controller_node  -> 能跑
#     ros2 run demo_nodes controller_node                -> No executable found
# 这是个特别容易卡住的坑，因为它"看起来编好了"，只有 ros2 run 失败。
#
# ★ 想让 ros2 run 之外也能直接在 PATH 里用，可以两处都装：
#     install(TARGETS controller_node monitor_node
#       DESTINATION lib/${PROJECT_NAME})
#     install(TARGETS controller_node monitor_node
#       DESTINATION bin)          # 额外再装一份到 bin/
#   但一般不需要，用 ros2 run 就好。
# -----------------------------------------------------------------------------
install(TARGETS
  controller_node
  monitor_node
  DESTINATION lib/${PROJECT_NAME}
)

# -----------------------------------------------------------------------------
# 【3】install 运行时资源：launch 文件和参数文件
#
# ★ 这是"源码目录 ≠ 运行时目录"最直观的例子。
#
#   你在 src/demo_nodes/launch/demo.launch.py 里改了东西，
#   不 colcon build 的话，ros2 launch 读到的还是 install/ 下的旧副本。
#   所以"改了立刻生效"在 ROS2 里是不成立的，资源文件一律要重新 install。
#
# ★ 末尾不要加斜杠："launch" 会拷成 share/demo_nodes/launch/...，
#   "launch/" 会把 launch 目录里的**内容**直接倒进 share/demo_nodes/，
#   于是 ros2 launch demo_nodes demo.launch.py 就找不到了。
#   （对 include/ 目录我们用了 "include/"，那是因为它的内容正好该直接进 include/。
#     规则一样，只是目的不同。）
#
# ★ DESTINATION share/${PROJECT_NAME} 是 ROS2 的资源目录约定。
#   ament_index_python 的 get_package_share_directory('demo_nodes') 返回的就是它。
# -----------------------------------------------------------------------------
install(DIRECTORY
  launch
  config
  DESTINATION share/${PROJECT_NAME}
)

# -----------------------------------------------------------------------------
# 【4】依赖导出 —— 本包是链条末端，没有下游，所以不需要 ament_export_*
#
# 一个包什么时候需要 ament_export_targets / ament_export_dependencies？
#     当它有**库**要被别的包链接时。
# 只有可执行文件、只被 ros2 run 调用的包，两个都不用写。
# demo_nodes 就是这种情况。
#
# ★ 反过来说：如果你发现某个包别人怎么都 link 不上，
#   第一件事是去那个包的 CMakeLists 里看看有没有这两行。
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# 【5】扩展练习（建议真的动手做一遍）
#
#   a) 加一个 src/legacy/ 子目录 + 一个 add_subdirectory(src/legacy)，
#      体会 CMAKE_CURRENT_SOURCE_DIR 和 PROJECT_SOURCE_DIR 的区别。
#
#   b) 加一个只订阅不发布的 logger 节点，用 target_link_libraries 而不是
#      ament_target_dependencies 来链接，对比两种写法的可读性。
#
#   c) 把 demo_tools 的 PUBLIC demo_math::demo_math 改成 PRIVATE，
#      重新 colcon build，看 controller_node 报什么错，然后改回来。
#      这一步做完，PUBLIC / PRIVATE 你就彻底不会记混了。
#
#   d) 把 demo_tools 的 add_library 从 SHARED 改成 STATIC，再编译。
#      install(EXPORT) 会报错说 demo_tools_internal 不在任何导出集合里。
#      想清楚为什么 SHARED 没这个问题 —— 这题答上来，CMake 链接模型就通了。
# -----------------------------------------------------------------------------

ament_package()
