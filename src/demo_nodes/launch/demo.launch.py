# =============================================================================
#  demo.launch.py —— 一个 launch 文件的最小可用版本
#
#  它和 CMake 的关系只有一条，但很关键：
#
#      这个文件必须被 install() 到 share/demo_nodes/launch/ 下，
#      ros2 launch 才能找到它。
#
#  换句话说：launch 文件不是"放在源码目录里就能用"的，
#  它是被当成**运行时资源**安装的，和头文件、可执行文件一样。
#  对应的 CMakeLists.txt 那一行是：
#
#      install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})
#
#  验证方法：ros2 launch 找不到文件时，去
#      install/demo_nodes/share/demo_nodes/launch/
#  看一眼文件到底有没有被装进去。九成是 install 写漏了。
# =============================================================================

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    # ★ get_package_share_directory 找的是 **install/** 目录，不是 src/ 目录。
    #   所以改了 params.yaml 之后必须 colcon build 才会生效。
    #   这是 ROS2 新手最常问的问题之一："我明明改了参数，怎么没用？"
    pkg_share = get_package_share_directory('demo_nodes')
    params_file = os.path.join(pkg_share, 'config', 'params.yaml')

    controller = Node(
        package='demo_nodes',
        executable='controller_node',
        # ★ name 必须和 params.yaml 里的顶层 key 完全一致
        name='controller',
        parameters=[params_file],
        output='screen',
    )

    monitor = Node(
        package='demo_nodes',
        executable='monitor_node',
        name='monitor',
        parameters=[params_file],
        output='screen',
    )

    return LaunchDescription([controller, monitor])
