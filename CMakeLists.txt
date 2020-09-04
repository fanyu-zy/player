# CMakeList.txt: ffplayer_synchronously 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.8)

include_directories("include")
link_directories("lib")

# 将源代码添加到此项目的可执行文件。
add_executable (mp4player w5player.cpp)
target_link_libraries(
	mp4player	#本项目的解决方案名称
	avcodec		#音视频编码核心库 , 用于音视频或图片编解码
	avdevice	#硬件采集 , 加速 , 显示 库
	avfilter	#音视频滤波器
	avformat	#音视频封装格式生成或解析 
	avutil		#该库中封装了一些公共的工具函数
	postproc	#封装了同步, 时间计算的简单算法
	swresample	#音视频编解码格式预设
	swscale		#原始视频格式转换库
	SDL2
	SDL2main
	OpenAL32
)
