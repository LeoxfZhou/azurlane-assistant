# 存档、备份与跨端交换

[返回首页](../README.md) · [指挥猫指南](COMMANDER_CATS.md) · [发布边界](PUBLISHING.md)

## 数据放在哪里

以下路径相对于当前 Mac 项目根目录；设置 `GAME_GUI_USER_DATA_DIR` 时个人存档根目录可被覆盖。

| 路径 | 内容 |
|---|---|
| `data/commander_cats/` | 图鉴、天赋、攻略观点、排行、固定路线等参考资料 |
| `data/fleet_ships/` | 编队用舰娘及形态图鉴、可选头像 |
| `data/ship_stories/` | 剧情图鉴、章节任务、奖励 |
| `data/equipment/` | 装备参考图鉴、评级与来源 |
| `data/input/` | 原始资料收件区，不自动作为正式数据读取 |
| `user_data/commander_cat_inventory.csv` | 指挥猫初始库存事实 |
| `user_data/commander_cat_instance_sequence.txt` | 实例编号高水位，避免删除后误复用编号 |
| `user_data/fleet_plans.json` | 真实编队方案 |
| `user_data/fleet_recommendations.json` | 保存的推荐及依赖信息 |
| `user_data/graduation.json`、`graduation_images/` | 离线培养观察与历史证据 |
| `user_data/ship_story_progress.csv` | 剧情状态 |
| `user_data/equipment_inventory.json` | 装备库存 |
| `cache/`、`build/` | 暂存结果和构建产物，不是库存 |

旧七类数量设置文件可能仍在存档中，但当前 GUI 编队不再靠它定义队伍。
不要按旧统一项目文档中的 `user_data/azur_lane/` 路径机械覆盖当前目录。

## Windows 库存导入

可以选择一个 ZIP，或同时选择下面两个文件：

- `commander_cat_inventory.csv`：精确字段 `instance_id,cat_id,nickname,initial_talent_ids`。
- `commander_cat_manifest.json`：包含 `format_version=2`、兼容 `rule_version`、CSV 原始字节的 SHA-256。

CSV 是 UTF-8；天赋使用规范 ID，以 ` + ` 分隔，只表示初始事实。
ZIP 根目录放这两个文件，不带子目录。
先检查／预览，再确认替换。版本、哈希、重复编号或天赋校验失败时不应继续导入。
规则版本声明不证明算法已对齐，两端仍需共同测试。

库存导入不覆盖编队或剧情进度；Mac 重新计算派生评级。日常库存包不包含证据截图。
完整恢复时还要保留编号高水位、个人设置等相关文件，不能把普通交换包当作完整备份。

## 编队导入

在独立“导入 Windows 编队”入口选择 `mac-fleet-plans.json`，预览后确认。
名称沿用交换格式，不代表只能由 Mac 导出。
只更改编队和历史推荐，不修改库存。导入结果需检查已有猫和参考资料，随后重新推荐所有方案。

## 多选导出

在指挥猫“设置 → 导出到 Windows”勾选需要的部分：

| 选择 | 导出文件 |
|---|---|
| 仅编队（含已保存推荐） | `mac-fleet-plans.json` |
| 仅指挥猫库存 | `commander_cat_inventory.zip`，内含 CSV 和清单 |
| 两者都选 | `commander-cat-transfer.zip`，内含上述 JSON 和库存 ZIP |
| 都不选 | 按钮禁用，不能导出 |

全选后先解压外层 ZIP，再分别在对应入口导入。
不包含星塔数据、舰船剧情进度或装备库存，也不包含模拟器配置和毕业截图。
装备有独立的导入导出入口。

## 备份与冲突

编队等存档使用原子替换及版本检查；这减少半写入风险，但不代替独立备份。
导入与部分模块变更有备份目录，不能假设每一次编辑都有完整历史。
损坏文件或旧预览冲突时，先保留原文件，不要反复用空数据覆盖。

完整本地备份建议：停止服务，再复制整个 `user_data/` 到私人位置。
恢复前先备份当前目录，确认程序／格式版本兼容，停止服务后再恢复；不要只凭文件名跨版本替换。
如果仅需交换库存或编队，优先使用 GUI 的校验导入。

## GitHub 状态与平台分工

当前**没有 GUI GitHub 一键同步**。上传／拉取代码不等于上传／拉取个人存档。
公开代码仓库忽略个人数据；私人同步仓库不能用公开仓库中的另一分支代替。

工作流约定是 Windows 负责扫描库存，Mac 检查评级并编辑方案；不是程序自动强制的权限规则。
现有界面支持双向文件交换，所以覆盖前仍要确认谁的数据更新。
Windows 模拟器功能尚未纳入此仓库，未来接入见 [Windows 提示词](WINDOWS_SETUP.txt)。
不把 GitHub 凭据、设备配置或个人截图提交到公开仓库。
