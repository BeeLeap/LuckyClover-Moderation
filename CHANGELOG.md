# 更新日志

## [Unreleased]

## [0.2.13] - 2026-11-06

### 兼容性

- 适配 LeviLamina 26.60.x，构建依赖更新为 LeviLamina 26.60.2 和 iListenAttentively 0.18.0。
- 适配 26.60 的 `mods` 模组目录，并保留旧 `plugins` 路径回退兼容。

### 修复

- 避免复制 26.60 中未导出完整拷贝符号的网络 packet 类型，修复链接失败。
- 将书本文本过滤迁移到 iListenAttentively 的 `ReceivePacketBeforeEvent`，修复模组启用失败。
- 修复 `BookEditPacket` 事件 ID 注册方式，确保书本过滤正常启用。

### 构建与发布

- 按 LeviLamina 官方模组模板整理 GitHub Actions 发布流程。
- 发布流程拆分为构建、更新发行版说明和上传附件三个阶段。
- 忽略本地发布暂存目录和压缩包产物。

## [0.2.11] - 2026-10-18

### 构建

- 对齐 LeviLamina 官方模组模板的 GitHub Actions 构建流程。
- 固定 Windows x64 服务端构建参数，并增加原生插件与 API 插件发布包。

## [0.2.10] - 2026-10-18

### 修复

- 在原始 `TextPacket` 入口过滤聊天消息，修复后台显示已过滤但客户端仍看到原文的问题。
- 修复书籍、铁砧和物品请求包的文本修改路径。
- 修复监听器启用失败时的回滚和词库热重载失败恢复。

### 发布

- 继续提供独立的 `LuckyClover-Moderation-API` 脚本桥接插件。
