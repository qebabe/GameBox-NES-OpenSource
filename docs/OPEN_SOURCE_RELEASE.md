# 开源发布清单

本项目公开主机和 ESP32-C3 手柄源码，不公开服务端源码。当前本地开发分支的历史曾经跟踪过服务端文件，因此不能直接把该分支及其父提交推送到公开仓库。

## 公开内容

- `src/`：主机、手柄、模拟器、板级适配和客户端联网逻辑；
- `test/`：不依赖私有服务实现的测试；
- `tools/`：串口控制、测试校验等公开开发工具；
- `docs/`、使用指南、硬件图片、许可证和构建配置。

## 永不公开的内容

- `tools/rom_api/`、`dist/`：服务端实现、部署配置和部署包；
- `src/service_config.local.h`：生产服务地址；
- `data/`、`rom/`、`game/`：商店数据、商业封面和用户 ROM；
- `firmware/`、`.pio/`：预编译固件及构建产物；
- `*.env`、数据库、日志、PID、私钥、证书和各类发布压缩包；
- `test-roms/*.nes`：第三方 ROM 二进制。公开仓库仅保留测试说明和哈希清单。

## 首次公开发布

最稳妥的方式是从当前工作树复制允许公开的文件到一个全新目录，不复制 `.git`；然后在新目录执行 `git init`，用面向公众的提交者姓名和邮箱创建首个提交。

如果需要保留既有公开仓库历史，也只能从确认安全的公开分支新建发布分支，再把当前净化后的最终文件树作为一次 squash 提交导入。禁止合并、rebase 或推送包含服务端父提交的本地开发分支。

发布前还要完成：

1. 确认 `git status --ignored` 中所有私有文件均处于忽略状态。
2. 对发布目录执行敏感信息扫描，并人工搜索生产域名、邮箱、IP、用户名和内部路径。
3. 确认 `git log --all -- tools/rom_api firmware data` 没有输出。
4. 只把确认无服务端历史的仓库或分支推送到公开远端。

不要依赖“删除后再提交”保护服务端代码：旧提交、标签和 Git 对象仍可能被下载。如果已经把含敏感信息的历史推送到公开远端，应先轮换所有真实凭据，再净化历史或更换公开仓库。

## 每次发布前

```bash
git status --short
git ls-files | rg '^(tools/rom_api|firmware|data|rom|game)/|service_config\.local\.h$|\.(nes|env|db|sqlite3?|pem|key|p12|pfx|zip|tar\.gz)$'
git grep -n -I -E 'BEGIN .*PRIVATE KEY|api[_-]?key|secret[_-]?key|access[_-]?token|password[[:space:]]*='
git grep -n -I '<请在本机替换为真实生产域名>'
```

以上命令中，除示例或说明文字外，敏感文件检查和生产域名检查都应无输出。扫描不能替代人工复核。
