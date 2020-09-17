# 基于ffmpeg+openAL+sdl的音视频同步播放器
## 代码使用方法  
项目编译成功后，在工程目录下会有个一个Debug或者Release文件夹  
文件夹内有运行生成的`mp4player.exe`应用程序文件  
在包含exe文件的路径下运行cmd，输入`mp4player.exe`和将想要播放的文件的路径  
例如像播放F盘中的fire.mp4文件  
应输入`mp4player.exe F:/fire.mp4`  
也可将想要播放的文件放在该文件夹下，直接输入文件名即可  
例如`mp4player.exe test.mp4`  

# 完善后的播放器  
## 代码使用方法  
编辑`CMakeLists.txt`，将第十行中的`w5player.cpp`改为`w6player.cpp`  
重新编译项目，对应文件夹内有运行生成的`mp4player.exe`应用程序文件  
运行输入同上  
按`空格键`实现暂停播放  
小键盘中`+`音量增大，`-`音量减小    
小键盘中`1`快进10秒，`2`快进30秒  
按`q`实现加速播放，按`s`实现减速播放  
调节范围[0.5 0.75 1 1.5 2.0]  
## 仍需完善  
未实现变速不变调
