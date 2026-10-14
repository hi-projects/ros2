# =============================================================================
#  demo_math —— C++ 库包
#
#  这个文件是整个例子里的重头戏。它回答一个 CMake 的核心问题：
#
#      「我写的库，怎么让别的包用起来？」
#
#  答案在 CMake 里叫 "export / import"，在 ament_cmake 里叫 ament_export_*。
#  搞懂这个文件，你就搞懂了 ROS2 里 90% 的 CMake 报错。
# =============================================================================

cmake_minimum_required(VERSION 3.8)
project(demo_math)

# -----------------------------------------------------------------------------
# 【1】C++ 标准 —— 注意这个 if 是故意写的
#
# 为什么不直接 set(CMAKE_CXX_STANDARD 17)？
# 因为 CMAKE_CXX_STANDARD 是个**缓存变量**，可能已经被外层（colcon、别的包、
# 命令行 -DCMAKE_CXX_STANDARD=20）设过了。直接赋值会把它覆盖掉，
# 用户明确指定的标准就被你悄悄改了。
#
# 这是 CMake 的一条通用原则：**库代码不要强改全局设置，只提供默认值。**
# 你会在这份文件的每一处看到这个原则的变体。
# -----------------------------------------------------------------------------
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)

# -----------------------------------------------------------------------------
# 【2】add_library —— 造一个 target
#
# ★ 这是理解现代 CMake 的第一道门：**一切皆 target**。
#   add_library / add_executable 造出来的东西，会带着一堆"属性"活着：
#     - 它依赖谁        (INTERFACE_LINK_LIBRARIES / LINK_LIBRARIES)
#     - 它的头文件在哪  (INTERFACE_INCLUDE_DIRECTORIES / INCLUDE_DIRECTORIES)
#     - 它要什么编译选项、宏定义、C++ 标准……
#
#   然后别的 target 通过 target_link_libraries() 引用它时，CMake 会自动
#   把该传的属性传过去。这个"自动传递"就是所谓 modern CMake。
#
# ★ target 的名字，惯例直接用 ${PROJECT_NAME}。这样包名 = 库名，
#   下游写 find_package(demo_math) + target_link_libraries(x demo_math::demo_math)
#   非常顺。不要用 "libdemo_math" 这种名字 —— CMake 会自己加 lib 前缀。
#
# SHARED vs STATIC：
#   SHARED 生成 .so，运行时需要动态链接器找得到它（见下面 HAS_LIBRARY_TARGET）；
#   STATIC 生成 .a，会被直接拷进下游的可执行文件，运行时无负担。
#   两者对"导出"的要求不同，后面 demo_tools 里会踩到这个区别。
# -----------------------------------------------------------------------------
add_library(${PROJECT_NAME} SHARED
  src/pid.cpp
)

# -----------------------------------------------------------------------------
# 【3】target_include_directories + 生成器表达式 —— 本文件最精妙的一处
#
# 问题：这个库的头文件在 include/ 目录下，编译本库的下游时需要能 include 到
#       "demo_math/pid.hpp"。但头文件的位置，在**编译期**和**安装后**是两个
#       完全不同的路径：
#
#        编译期（还在你的工作空间里）:  <source>/demo_math/include
#        安装后（下游 find_package）:  <install_prefix>/include
#
# 直接写死任何一个都会有一半场景是错的。生成器表达式 $<...> 解决这个问题：
# 它是"延迟求值"的 —— CMake 在生成构建系统时才决定用哪一边。
#
#   $<BUILD_INTERFACE:...>    在这棵构建树内部使用它时，取这个路径
#   $<INSTALL_INTERFACE:...>  这个包被 install 出去、被下游引用时，取这个路径
#
# ★ 注意 INSTALL_INTERFACE 里写的是相对路径 "include"，不是绝对路径。
#   install(EXPORT) 会自动把它拼到下游的安装前缀上。
#   写绝对路径（比如 /home/you/ros2_ws/install/demo_math/include）就完蛋了 ——
#   那个路径只在你的机器上有效，别人一拉代码就编不过。
#
# ★ PUBLIC 是必须的，不能写 PRIVATE。
#   PRIVATE = 只有编译 demo_math 自己时用这个 include 路径；
#   PUBLIC  = 编译 demo_math 自己 + 编译所有链接了 demo_math 的下游时都用。
#   头文件是要给下游看的，所以必须 PUBLIC。
#   写错的后果：下游报 "fatal error: demo_math/pid.hpp: No such file or directory"。
# -----------------------------------------------------------------------------
target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)

# -----------------------------------------------------------------------------
# 【4】target_compile_features —— 更精确地表达"我需要 C++17"
#
# 上面第【1】节设的是"全局默认值"，而这一行说的是：
#   "任何用我的人，至少要用 C++17 编"（PUBLIC = 传递给下游）
#
# 差别在于：全局 CMAKE_CXX_STANDARD 只影响本目录；compile_features 会跟着
# target 走，下游哪怕自己设了 C++14，链接本库时也会被抬到 17。
# 库包应该用这个，而不是依赖全局变量。
# -----------------------------------------------------------------------------
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

# -----------------------------------------------------------------------------
# 【5】ament_target_dependencies —— ament 版的"链接一个包"
#
# 签名（简化）：ament_target_dependencies(<target> [PUBLIC|INTERFACE|SYSTEM] <包名...>)
#
# ★ 它和标准 CMake 的对应关系，直接看 ament 源码
#   (ament_cmake_target_dependencies/cmake/ament_target_dependencies.cmake)：
#
#     对每个包名，它去查 ${包名}_TARGETS（比如 rclcpp::rclcpp）；
#     有现代 target 就用 target_link_libraries(target ... rclcpp::rclcpp)；
#     没有就退回用老变量 rclcpp_INCLUDE_DIRS / rclcpp_LIBRARIES，
#     然后挨个 target_include_directories / target_link_libraries。
#
#   所以它**不是**什么新机制，它就是这两条标准命令的打包：
#
#     ament_target_dependencies(t PUBLIC rclcpp)
#     ≈  target_link_libraries(t PUBLIC rclcpp::rclcpp)
#        + 把 rclcpp 的 include 目录也挂上
#
# ★ 为什么还要用它？
#   - 它是 ROS2 文档和几乎所有现存代码的写法，不学看不懂别人的代码
#   - 它能同时兼容"导出老式变量的包"和"导出现代 target 的包"
#
# ★ 为什么 CMake 老手更喜欢直接写 target_link_libraries？
#   - 能精确表达 PUBLIC / PRIVATE / INTERFACE（老式变量版本做不到 PRIVATE）
#   - 链接关系一眼可见，不用去猜 ament 宏干了什么
#   - IDE (CLion/VSCode) 的 CMake 集成能正确解析
#
#   两个写法都合法，本工程两种都用给你对比：demo_math 用 ament 宏，
#   demo_tools 用标准 CMake 命令，效果完全一样。
#
# ★ 这里的 PUBLIC 是必须的：pid.hpp 里用了 rclcpp::Duration，
#   下游只要 include 这个头文件，就需要 rclcpp 的头文件路径。
#   写成 PRIVATE 的话，下游编译时会报找不到 rclcpp/duration.hpp。
# -----------------------------------------------------------------------------
ament_target_dependencies(${PROJECT_NAME} PUBLIC
  rclcpp
)

# -----------------------------------------------------------------------------
# 【6】install(TARGETS ... EXPORT ...) —— 从"能编"到"能被别人用"的分水岭
#
# 前三行的 install(DIRECTORY) 好理解：把头文件拷到安装目录。
# 真正关键的是 install(TARGETS) 里的 EXPORT 参数。
#
# ★ 先说清楚：**没有 install()，colcon build 一样能成功。**
#   区别是：只有 install 过的东西，才会出现在 install/ 目录里，
#   而下游包 find_package() 找的正是 install/ 目录。
#   所以"本地能编过、下游找不到"这类问题，八成是 install 写漏了。
#
# ★ EXPORT demo_mathTargets 做了什么事？
#   它把这批 target 的**属性快照**（include 目录、链接依赖、编译特性……）
#   记录下来，生成一个 CMake 文件。下游 find_package(demo_math) 时，
#   执行的就是这个文件，于是 CMake 就"凭空"多出了 demo_math::demo_math
#   这个 imported target，并且带着正确的属性和依赖。
#
#   这就是 CMake 里"我的库怎么给别人用"的完整答案。
#   注意这里只是**起个集合名**（demo_mathTargets），还没真正导出；
#   真正让下游能找到的是下面第【7】节的 ament_export_targets。
#
# ★ ARCHIVE / LIBRARY / RUNTIME 的区别（在 Linux 上）：
#     RUNTIME -> 可执行文件，装到 bin/
#     LIBRARY -> 动态库 .so，装到 lib/
#     ARCHIVE -> 静态库 .a，装到 lib/
#   三个都写上是为了跨平台（Windows 上动态库算 RUNTIME，落到 bin/ 而不是 lib/）。
#
# ★ DESTINATION 写相对路径（lib、bin），会拼到 CMAKE_INSTALL_PREFIX 上。
#   绝对路径只在你自己机器上有效 —— 又是一条"不要写死路径"。
# -----------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME}
  EXPORT ${PROJECT_NAME}Targets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

# 把 include/ 下的目录结构原样拷过去，变成 install/demo_math/include/demo_math/pid.hpp
# ★ 末尾那个 / 很关键："include/" 表示拷目录**里面的内容**，
#   而 "include"（不带斜杠）会拷成 include/include/...
install(DIRECTORY include/
  DESTINATION include
)

# -----------------------------------------------------------------------------
# 【7】ament_export_targets —— 让上面那个导出集真正生效
#
# ★ 参数就是我们上面 install(TARGETS ... EXPORT ...) 里起的那个名字，
#   两边必须一字不差。写错了不会有报错，只会在下游 find_package 时
#   报 "找不到 demo_math::demo_math" —— 非常难查。
#
# ★ 它会自动给导出集加上命名空间 ${PROJECT_NAME}::，
#   于是 target 名字从 demo_math 变成 demo_math::demo_math。
#
#   为什么要加 :: ？这是 CMake 的约定，用来区分三种东西：
#     demo_math            —— 本目录里 add_library 出来的"本地 target"
#     demo_math::demo_math —— 从别的包 import 进来的 target
#   （另一种老写法是 ${PROJECT_NAME}_LIBRARIES 变量，现在已经不推荐了。）
#   你可以在下游的 CMakeLists 里看到这个区别：demo_tools 里写的是
#   target_link_libraries(... demo_math::demo_math)，那个 :: 就是这个来的。
#
# ★ HAS_LIBRARY_TARGET 是什么意思？
#   demo_math 是 SHARED 库，编译出来是 install/demo_math/lib/libdemo_math.so。
#   运行时动态链接器要去哪找这个 .so？它默认只看 /usr/lib 这类系统目录，
#   根本不知道你的工作空间在哪，于是程序启动就报
#       error while loading shared libraries: libdemo_math.so: cannot open shared object file
#
#   加上 HAS_LIBRARY_TARGET，ament 会给这个包生成一个**环境钩子**，
#   你 source install/setup.bash 的时候，它自动把
#   install/demo_math/lib 追加到 LD_LIBRARY_PATH。
#
#   编译完你可以去看这个钩子文件：
#       install/demo_math/share/demo_math/hook/ld_library_path_lib.dsv
#   .dsv = "directory separator value"，就是一堆用冒号分隔的路径。
#
#   如果当初 add_library 用的是 STATIC，就不需要这个选项了（.a 被直接拷进可执行文件）。
# -----------------------------------------------------------------------------
ament_export_targets(${PROJECT_NAME}Targets HAS_LIBRARY_TARGET)

# -----------------------------------------------------------------------------
# 【8】ament_export_dependencies —— 手动补上传递依赖
#
# ament_export_targets 导出了"我自己的 target"，但它导不出"我依赖的包"。
# 下游 find_package(demo_math) 之后会得到一个 demo_math::demo_math，
# 这个 target 的 INTERFACE_LINK_LIBRARIES 里写着 rclcpp::rclcpp。
# 可是此时 rclcpp::rclcpp 还不存在 —— 下游没 find_package(rclcpp)，
# 于是 CMake 直接报错：
#     The link interface of target "demo_math::demo_math" contains:
#         rclcpp::rclcpp
#     but the target was not found.
#
# ament_export_dependencies(rclcpp) 就是解决这个的：它会让 ament 在生成的
# demo_mathConfig.cmake 里写进一句 find_dependency(rclcpp)，
# 于是下游只要 find_package(demo_math)，rclcpp 会被自动带上。
#
# ★★ 这是 ament_cmake 最需要建立直觉的一点：
#
#     标准 CMake 的世界里，依赖传递是**自动**的：
#       你 PUBLIC 链接了谁，下游就自动继承谁（因为都写在 target 属性里）。
#
#     ament 的世界里，这个传递是**半自动**的：
#       target 之间的链接关系会通过 install(EXPORT) 自动传；
#       但"要在下游触发哪个 find_package"必须你用 ament_export_dependencies 手动声明。
#
#   记忆方法：**凡是出现在你公开头文件里的第三方类型，都要 export_dependencies。**
#   （只出现在 .cpp 里的，PRIVATE 链接即可，不用导出。）
#
# ★ 顺序要求：所有 ament_export_* 必须在 ament_package() 之前。
# -----------------------------------------------------------------------------
ament_export_dependencies(rclcpp)

# -----------------------------------------------------------------------------
# 【9】ament_package() —— 最后一条命令
#
# 它把上面所有 ament_export_* 攒下的内容，写成下游 find_package 时要执行的
# 那几个文件，并且生成一个"本包已被正确 ament 化"的标记。
#
# 编译完之后强烈建议你去这几个地方看看（见 README 的"编译产物导览"）：
#   install/demo_math/share/demo_math/cmake/demo_mathConfig.cmake
#   install/demo_math/share/demo_math/cmake/demo_mathTargets.cmake
# 你会发现所谓"魔法"其实就是一堆可以读的 CMake 代码。
# -----------------------------------------------------------------------------
ament_package()
