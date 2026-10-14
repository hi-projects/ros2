<#
    一键进入 ROS2 Humble 容器。

    用法（在仓库根目录，PowerShell 里）：
        powershell -ExecutionPolicy Bypass -File .\docker\run.ps1

    进容器后你会停在 /ws，ROS2 环境已经 source 好，直接 colcon build 即可。

    关于 --symlink-install：
      这里**故意没有**加这个参数。
      colcon build --symlink-install 会把 install/ 下的文件做成指向 src/ 的符号链接，
      于是改了 launch / yaml 不用重新 build 就生效。
      开发时很方便，但它会掩盖"源码目录 ≠ 运行时目录"这个事实，
      会让你对 install() 到底装了什么失去直觉。
      学习阶段先用普通 colcon build，等理解了再开。

    关于镜像源：
      国内直连 Docker Hub 基本不通。默认用 daocloud 镜像源。
      如果你已经配好了 Docker Desktop 的 registry-mirrors，
      可以设环境变量换回官方名字：
          $env:ROS_IMAGE="ros:humble-ros-base"
#>

$ErrorActionPreference = "Stop"

# 仓库根目录 = 本脚本所在目录的上一级
$root = Split-Path -Parent $PSScriptRoot

$image = if ($env:ROS_IMAGE) { $env:ROS_IMAGE } else { "docker.m.daocloud.io/ros:humble-ros-base" }

Write-Host "镜像: $image"
Write-Host "挂载: $root -> /ws"
Write-Host ""

docker run --rm -it `
    --name ros2_cmake_demo `
    -v "${root}:/ws" `
    -w /ws `
    $image `
    bash -lc "source /opt/ros/humble/setup.bash && echo 'ROS2 环境就绪: '\$ROS_DISTRO && exec bash"
