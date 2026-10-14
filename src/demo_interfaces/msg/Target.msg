# 自定义消息：控制器每一拍发布的"跟踪状态"快照
#
# 语法规则（rosidl 自定义的，不是 C++ 也不是 CMake）：
#   类型 字段名
#   数组用  float64[] name / float64[4] name
#   常量用  float64 MAX=1.0
#
# 这里的 std_msgs/Header 是**跨包依赖**。它会在下面两个地方各声明一次：
#   - CMakeLists.txt : rosidl_generate_interfaces(... DEPENDENCIES std_msgs)
#   - package.xml    : <depend>std_msgs</depend>
# 前者给代码生成器看，后者给 colcon/rosdep 看。少了任何一个都编不过或顺序错。

std_msgs/Header header

float64 setpoint    # 目标值 r(t)
float64 measured    # 被控对象的实测值 y(t)
float64 error       # 误差 e(t) = setpoint - measured
float64 output      # 控制器输出 u(t)
