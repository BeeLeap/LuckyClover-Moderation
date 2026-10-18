# LuckyClover-Moderation-API

这是 `LuckyClover-Moderation` 的 LLSE QuickJS API 桥接插件，不依赖
`LuckyClover-Plugin` 核心插件。

命名空间：`LuckyCloverModeration`

- `filterText(text)`：返回过滤后的文本；Moderation 未加载时返回原文。
- `containsBannedWord(text)`：返回是否发生过滤。
- `reload()`：重新读取 Moderation 的配置和静态词库。

工会、VIP、商城和 TPA 会在保存用户可自定义文本前惰性调用这个 API。
请和 `LuckyClover-Moderation` 放在同一个服务器插件目录中；不需要安装
`LuckyClover-Plugin`。

