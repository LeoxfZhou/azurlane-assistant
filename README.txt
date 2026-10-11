碧蓝航线助手（独立项目）

双击本目录的“启动碧蓝航线助手.command”。启动器会寻找 CMake（支持 CLion 内置版本），增量编译并自动打开浏览器。
关闭启动终端或按 Ctrl+C 停止本游戏服务。星塔旅人可以同时独立运行。

CLion：打开此 AzurLane 文件夹，运行 azur_lane_gui 目标。
开发验证：cmake -S . -B build；cmake --build build；ctest --test-dir build --output-on-failure。
HTTP 测试需要允许监听本机端口；测试使用临时存档。

目录：src 是 C++ 服务及业务逻辑；web 是网页；tools 是更新与导入导出脚本；tests 是测试。
data 保存基础资料和原始输入；user_data 保存个人库存、编队、进度、历史证据与同步备份。
cache 保存临时结果；build 是生成目录。搬移项目时请携带整个文件夹，尤其是 user_data。
启动器检测到旧位置的构建缓存时，会保留为 build.previous.* 并在当前位置重建。

2026-10-11：从统一游戏助手拆分，个人数据按文件 SHA-256 核对迁移。
旧统一项目和迁移清单保存在 game/.migration-backup-20261011；该备份不参与新项目运行。
