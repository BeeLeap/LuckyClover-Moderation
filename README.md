# LuckyClover-Moderation

独立的 LeviLamina 原生文本输入违禁词过滤插件。

插件覆盖聊天、书籍标题/作者/正文、铁砧重命名、普通告示牌和悬挂告示牌，在文本提交前修改违禁内容，不依赖 `iListenAttentively-LseExport` 或 LLSE。

普通告示牌使用 `iListenAttentively` 的 `PlayerEditSignBeforeEvent`，悬挂告示牌、书籍和铁砧使用 LeviLamina 服务端网络包入口处理。

## 构建

当前目标为 LeviLamina 26.51.x，依赖上游 `LiteLDev/iListenAttentively` 0.17.0 或更新版本。该上游的 v0.17.0 已明确适配 LeviLamina 26.51。

需要准备：

- Visual Studio 2022 C++ 工具链（也可使用 clang-cl）
- xmake
- LeviLamina 开发依赖
- 不需要单独下载 iListenAttentively SDK；服务器运行时安装匹配版本即可

直接构建：

```powershell
xmake f -y -p windows -a x64 -m release
xmake
```

## 配置

复制 `config.example.json` 为运行时 `config.json`。默认读取本插件中的分类词库：

`data/banned-words`

自定义词和白名单均支持温和的大小写、全半角 ASCII 归一化。白名单优先于内置词和自定义词。

## LLSE API

系列脚本插件可以通过单独安装 `LuckyClover-Moderation-API` 使用
`LuckyCloverModeration.filterText(text)`、`containsBannedWord(text)` 和
`reload()`。这个桥接插件只读取本插件的配置和静态词库，不依赖
`LuckyClover-Plugin`；工会、VIP、商城和 TPA 已在保存自定义文本前接入。

如果首次启动时没有本插件配置，但发现旧核心配置，会自动迁移 `LuckyClover-Plugin/config.json` 中的 `bannedWords` 设置。

## 运行时依赖

服务端必须同时安装与当前 LeviLamina ABI 匹配的 `iListenAttentively`。插件通过 LeviLamina 事件总线订阅原生聊天事件和 iListenAttentively 的完整告示牌事件 ID，并使用通用事件序列化接口修改告示牌文本，因此不需要 iListen SDK 或 import library。本插件也不依赖 LseExport。
