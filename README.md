# 碧蓝航线助手

本地浏览器 GUI：指挥猫初始天赋鉴定、库存和编队规划、舰船个人剧情进度及装备管理。
服务仅监听 `127.0.0.1`。评分属于有来源标记的规则模型，不是天赋出现概率或伤害模拟。

## Mac 启动

需要 C++17 编译器（Apple Command Line Tools）、CMake 3.20+、Python 3.10+。
双击 `启动碧蓝航线助手.command`，会增量构建并打开浏览器。
脚本支持系统 CMake 和 CLion 内置 CMake，不需要启动 CLion。

也可以在项目目录执行：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/azur_lane_gui
ctest --test-dir build --output-on-failure
```

Node.js 为可选测试依赖，用于前端语法与逻辑测试。
关闭服务终端或按 Ctrl+C 停止服务。不要把此开发用本地服务暴露到公网。

## 目录与平台

- 根目录当前是 Mac 实现：`src/`、`web/`、`tools/`、`tests/`。
- `data/` 是公共参考资料，不是个人库存。
- `user_data/` 是个人库存、编队、推荐、观察记录及进度；Git 忽略此目录。
- `cache/`、`build/`、`exports/`、原始输入与旧迁移种子也不上传。
- Windows 以后放在独立 `windows/`，保留模拟器专属实现，不覆盖根目录 Mac 版本。

新用户从空库存和未获取的剧情状态开始。已有本地存档不会因构建而被覆盖。
公开版本不附带第三方头像、原始攻略或证据截图；缺图显示占位图。
可通过程序已有的数据刷新功能获取支持的本地资源，请自行遵守来源网站的使用条件。

## 私人数据同步

程序代码与私人存档必须分开管理。此公开仓库不保存个人数据。
当前仍使用 GUI 的文件导出与校验导入；GitHub 一键上传／下载尚未实现。
更新程序代码不等于同步存档。请勿对正在运行的 `user_data/` 直接执行 Git 覆盖。
跨端约定见 [数据边界](docs/PUBLISHING.md) 和 [Windows 接入提示词](docs/WINDOWS_SETUP.txt)。

## 素材与许可

基础资料来自碧蓝航线 WIKI，培养观点与排行另在数据文件中标记来源。
详见 [来源说明](docs/SOURCES.md)。本项目不是官方产品。
目前未授予代码开源许可证；公开可读不代表授予任意修改、再分发许可。
第三方素材权利不因本项目公开而转移。
