# =============================================================================
#  demo_interfaces —— 自定义消息包
#
#  这个包一个可执行文件都不编，它只做一件事：
#  把 .msg / .srv 变成 C++ 头文件（代码生成 = code generation）。
#
#  它是理解「ROS2 的代码生成是怎么挂在 CMake 上的」最好的样本。
#  全文一共只有 5 条有效命令，我们逐条拆。
# =============================================================================

# -----------------------------------------------------------------------------
# 【1】cmake_minimum_required —— 不是"随便写个低版本比较安全"
#
# 这个数字决定了两件事：
#   a) 你能用哪些 CMake 语法（低版本会禁掉新语法）
#   b) CMake 的"策略"（policy）默认值，即某些命令的行为方式
# 写得太高：老机器上直接 configure 失败。
# 写得太低：你以为在用现代写法，其实 CMake 在按老规矩解释你的命令。
#
# Humble 官方要求 >= 3.8（Ubuntu 22.04 自带 3.22）。这里就按最低要求写。
# -----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.8)

# -----------------------------------------------------------------------------
# 【2】project() —— 定义变量 + 标记语言 + 声明版本
#
# 执行完这一行，你自动获得这些变量（后面会反复用到）：
#   PROJECT_NAME             = demo_interfaces
#   PROJECT_SOURCE_DIR       = 本 CMakeLists.txt 所在目录
#   CMAKE_CURRENT_SOURCE_DIR = 同上（有 add_subdirectory 时两者才不同）
#   PROJECT_BINARY_DIR       = build 目录下对应的位置
#
# ★ PROJECT_VERSION 呢？这里**是空的**。
#
#   这是 CMake 原生行为：只有写成 project(demo_interfaces VERSION 1.2.3)
#   才会有 PROJECT_VERSION，不写 VERSION 参数它就是空字符串。
#   （实测：纯 CMake 项目里 project(demo) 之后的 PROJECT_VERSION = 空。）
#
#   那 ROS2 的版本号 0.1.0 在哪？在另一个变量里，名字是
#       demo_interfaces_VERSION        ← 包名 + _VERSION，不是 PROJECT_VERSION
#
#   它是这么来的：ament_package_xml() 会调用一个 Python 脚本
#       share/ament_cmake_core/cmake/core/package_xml_2_cmake.py
#   把 package.xml 解析成 CMake 变量，写进 build 目录下的 package.cmake：
#       set(_AMENT_PACKAGE_NAME "demo_interfaces")
#       set(demo_interfaces_VERSION "0.1.0")
#   然后 include 它。ament_package() 再用它去设置 PACKAGE_VERSION。
#
#   ★ 这就是 ament_cmake 的本质：**一个把 package.xml 翻译成 CMake 变量的翻译层。**
#     CMake 不认识 XML，package.xml 也不认识 CMake，两边靠一个 Python 脚本接起来。
#
#   ★ 也正因为如此，project() 里的名字**必须**和 package.xml 的 <name> 一致 ——
#     ament_package_xml() 会拿 PROJECT_NAME 去和解析结果比对，不一致直接报错。
#     它不比对版本号，所以版本号只写一处（package.xml）就够了。
#
# ★ 一条容易踩的坑：CMAKE_PROJECT_NAME 只被**顶层** project() 设置一次，
#   而 PROJECT_NAME 每次 project() 都会重写。子目录里要用当前包名，用 PROJECT_NAME。
# -----------------------------------------------------------------------------
project(demo_interfaces)

# -----------------------------------------------------------------------------
# 【3】find_package —— CMake 里最重要的一条命令，没有之一
#
# 它做的事情是：去磁盘上找某个包的"配置文件"（叫 <Package>Config.cmake 或
# <package>-config.cmake），把那个文件当脚本执行掉。执行的效果有两个：
#   a) 定义一批 imported target，比如 rclcpp::rclcpp
#   b) 设置一批变量，比如 rclcpp_INCLUDE_DIRS、rclcpp_LIBRARIES、rclcpp_FOUND
#
# ★ 去哪找？CMake 的官方术语是「prefix 搜索」：
#
#   第一步：收集一批 **prefix**（前缀目录）。
#           来源有很多，最常打交道的两个是：
#             CMAKE_PREFIX_PATH  ——  冒号分隔的列表，ROS2 主要靠它
#             <包名>_DIR         ——  直接指向配置文件所在目录，可精确指定
#           此外还有 <包名>_ROOT、PATH 环境变量、CMAKE_INSTALL_PREFIX、
#           系统目录 /usr/local /usr 等。
#
#   第二步：在每个 prefix 下依次试若干**目录布局**，找那两个文件名。
#           实测（CMAKE_PREFIX_PATH=/pa）CMake 按这个顺序试：
#             /pa/ccConfig.cmake                     ← 前缀根目录
#             /pa/cc-config.cmake                    ← 下划线命名，小写
#             /pa/share/cc/ccConfig.cmake            ← <prefix>/<name>/
#             /pa/share/cc/cc-config.cmake
#             /pa/share/cc/cmake/ccConfig.cmake      ← ★ ROS2 用的就是这个
#
#           实测能被识别的布局（每种单独放到一个前缀里都能命中）：
#             <prefix>/cc/ccConfig.cmake
#             <prefix>/cc/cmake/ccConfig.cmake
#             <prefix>/lib/cmake/cc/ccConfig.cmake
#             <prefix>/share/cmake/cc/ccConfig.cmake
#             <prefix>/cmake/ccConfig.cmake
#             <prefix>/share/cc-1.2/cmake/ccConfig.cmake   ← 带版本号的目录
#             <prefix>/share/cc/cmake/cc-config.cmake      ← 小写 -config
#
#   第三步：**找到第一个就停**。所以 prefix 的先后顺序 = 优先级。
#           CMAKE_PREFIX_PATH 里靠前的 prefix 会赢。
#
# ★ ROS2 用的是最后一个布局：install/<包名>/share/<包名>/cmake/。
#   ament_package() 就是刻意把文件生成到那个位置，好让 CMake 的标准搜索能命中。
#
# ★★ 别再靠猜。CMake 有个开关能把上面整个过程逐行打印出来：
#         cmake -DCMAKE_FIND_DEBUG_MODE=ON ...
#     它会列出所有 prefix 来源、每个 prefix 下试过的完整路径、以及最终命中在哪。
#     find_package 找不到包时，第一件事就是开它，别去读文档猜。
#
# REQUIRED：找不到就直接 FATAL_ERROR 退出。
#           不加的话，find_package 失败也只是一笑了之，等到链接时才炸，
#           报错信息会离真正的原因十万八千里。
# -----------------------------------------------------------------------------

# 3.1 ament_cmake 本身
#     ★ 关键认知：ament_cmake 不是"另一个 CMake"，它是往 CMake 里注入一批宏。
#     find_package(ament_cmake) 之后，ament_package() / ament_target_dependencies()
#     / ament_export_targets() 这些宏才存在。没有这一行，下面全部报
#     "Unknown CMake command"。
find_package(ament_cmake REQUIRED)

# 3.2 消息生成器
#     它提供 rosidl_generate_interfaces() 这个宏。
#     注意它是 build_depend（只在编译期要），所以 package.xml 里也写 build_depend。
find_package(rosidl_default_generators REQUIRED)

# 3.3 我们自己的 msg 里嵌了 std_msgs/Header，所以要能找到 std_msgs
find_package(std_msgs REQUIRED)

# -----------------------------------------------------------------------------
# 【4】rosidl_generate_interfaces —— 整个包的核心，也是最大的"魔法"
#
# 签名（简化）：
#   rosidl_generate_interfaces(<target_name> <接口文件...> [DEPENDENCIES <包...>])
#
# 第一个参数是"目标名"，惯例直接写 ${PROJECT_NAME}。
# 它会生成一个**同名 CMake target**，所以后面别的包才能
#   target_link_libraries(自己的target demo_interfaces::demo_interfaces)
# 这就是「消息包」和「普通库包」在 CMake 层面的统一之处 —— 消息也是一种库。
#
# 这一行背后实际做的事情（编译完去 build/demo_interfaces/ 里看得到）：
#   1) 为每个 .msg/.srv 生成 C++ 结构体头文件
#        build/.../rosidl_generator_cpp/demo_interfaces/msg/target__struct.hpp
#   2) 生成 C 版本、Python 版本、类型支持(typesupport)代码
#   3) 生成 CMake 的依赖关系文件，让下游包能 resolve 到这些类型
#   4) 把它们编译成库
#
# ★ DEPENDENCIES 是给"代码生成器"看的，不是给链接器看的。
#   少了它，生成头文件时找不到 std_msgs 的 C 类型定义，编译期报 not declared。
#   package.xml 里的 <depend>std_msgs</depend> 则是给 colcon 排构建顺序用的。
#   两处都要写，而且必须一致 —— 这是 ROS2 新手最常见的坑之一。
# -----------------------------------------------------------------------------
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/Target.msg"
  "srv/Reset.srv"
  DEPENDENCIES std_msgs
)

# -----------------------------------------------------------------------------
# 【5】ament_export_dependencies —— "我用过的包，我的下游也要能找到"
#
# 消息包编译出来的库，运行时需要 rosidl 的运行时库支持（类型支持、序列化）。
# 下游包只 find_package(demo_interfaces) 是不够的，它还需要 rosidl_default_runtime。
# 这一行会让 ament 在生成的 demo_interfacesConfig.cmake 里写进一句
#     find_dependency(rosidl_default_runtime)
# 于是下游 find_package(demo_interfaces) 的时候，这句会被自动执行。
#
# ★ 这是 ament_cmake 的一个核心概念：**传递依赖要手动声明**。
#   标准 CMake 靠 target 的 INTERFACE 属性自动传递，ament 的宏不会，
#   所以它提供了 ament_export_* 系列宏让你手动补上。
#   后面 demo_math 和 demo_tools 会把这个概念讲透。
# -----------------------------------------------------------------------------
ament_export_dependencies(rosidl_default_runtime)

# -----------------------------------------------------------------------------
# 【6】ament_package() —— 每个 ament 包的最后一个命令，必须是最后一行
#
# 它做了什么：
#   1) 把本包注册到 ament 索引里（生成 share/<pkg>/package.xml 等）
#   2) 生成 <pkg>Config.cmake / <pkg>Config-version.cmake ——
#      这俩就是别的包 find_package(本包) 时被执行的文件，
#      也就是"魔法"落地的地方
#   3) 把前面所有 ament_export_* 宏攒下来的内容写进配置文件
#
# ★ 因为第 3 点，所有 ament_export_* 宏都必须在 ament_package() **之前**调用。
#   顺序反了 ament 会直接报 FATAL_ERROR（不是静默失败，这点做得挺好）。
#
# ★ 另一个容易困惑的点：本文件里从头到尾没有 install()。
#   消息库的 install 规则是 rosidl_generate_interfaces() 内部帮你装好的。
#   想确认的话，编译完执行 `ls install/demo_interfaces/` 看看。
# -----------------------------------------------------------------------------
ament_package()
