# Windows 局域网 Gitea 与 Unreal Git LFS 协作指南

## 1. 文档目标

本文说明如何把一台 Windows 电脑配置成局域网 Git 服务器，让其他电脑能够对 Unreal Engine 项目执行：

- `git clone`、`git pull` 和 `git push`；
- 下载和上传 `.uasset`、`.umap` 等 Git LFS 文件；
- 使用独立账号控制读写权限；
- 通过 Pull Request 审核代码；
- 排查端口、重定向和 Git LFS `404 Not Found` 等问题。

本文以 BrokenEdge 项目当前环境为例：

| 项目 | 当前示例值 |
| --- | --- |
| 服务器操作系统 | Windows |
| 服务器局域网地址 | `192.168.1.111` |
| Gitea HTTP 端口 | `3000` |
| Gitea 安装目录 | `C:\gitea` |
| 本地源仓库 | `C:\UnrealProjects\BrokenEdge` |
| Gitea 仓库 | `Blq/BrokenEdge` |
| 主要开发分支 | `AfterCreateSession` |

这些值可能随 IP、账号、仓库转让和分支调整而变化。执行命令前应先用实际值替换示例。

## 2. 系统结构与职责边界

```text
开发者电脑 A                         Windows Gitea 服务器
┌─────────────────────┐             ┌────────────────────────────┐
│ Git 提交、分支、标签 │──HTTP:3000─▶│ Gitea Git 仓库             │
│ Git LFS 客户端       │──HTTP:3000─▶│ Gitea LFS 存储             │
└─────────────────────┘             │ C:\gitea\data               │
                                    └────────────────────────────┘
开发者电脑 B                                      ▲
┌─────────────────────┐                           │
│ Git + Git LFS        │───────────────────────────┘
└─────────────────────┘
```

普通 Git 仓库存放提交、分支以及文本内容。Git LFS 文件在 Git 提交中只是一个很小的指针，真实的 `.uasset` 或 `.umap` 数据由 LFS 服务单独保存。

因此，以下两件事必须同时成功：

1. Git 提交和分支已经推送到 Gitea；
2. 提交引用的 LFS 对象也已经上传到 Gitea。

只完成第一步会出现“Git 对象克隆成功，但 checkout 时 LFS 下载失败”的情况。

## 3. 服务器准备

### 3.1 必要软件

服务器需要安装：

- Git for Windows；
- Git LFS；
- Gitea Windows AMD64 版本。

检查 Git 和 Git LFS：

```powershell
git --version
git lfs version
```

初始化当前 Windows 用户的 Git LFS：

```powershell
git lfs install
```

### 3.2 推荐目录

```text
C:\gitea\gitea.exe
C:\gitea\custom\conf\app.ini
C:\gitea\data\gitea.db
C:\gitea\data\gitea-repositories
C:\gitea\data\lfs
```

SQLite 适合个人或局域网小团队。Gitea 仓库目录和 LFS 目录应定期一起备份。

### 3.3 首次运行

```powershell
C:\gitea\gitea.exe web --work-path C:\gitea
```

在服务器浏览器中打开：

```text
http://127.0.0.1:3000
```

首次安装页面可使用：

```text
数据库：SQLite3
数据库文件：C:\gitea\data\gitea.db
仓库根目录：C:\gitea\data\gitea-repositories
LFS 根目录：C:\gitea\data\lfs
HTTP 服务端口：3000
```

`127.0.0.1` 只适合服务器本机访问，不能作为局域网客户端使用的服务器域名。

## 4. Gitea 网络配置

### 4.1 正确设置服务器地址

打开：

```text
C:\gitea\custom\conf\app.ini
```

确保 `[server]` 使用局域网 IP：

```ini
[server]
SSH_DOMAIN = 192.168.1.111
DOMAIN = 192.168.1.111
HTTP_PORT = 3000
ROOT_URL = http://192.168.1.111:3000/
DISABLE_SSH = true
LFS_START_SERVER = true
```

确认 LFS 路径：

```ini
[lfs]
PATH = C:/gitea/data/lfs
```

修改 `app.ini` 后必须重启 Gitea。

### 4.2 为什么不能把域名设为 `127.0.0.1`

`127.0.0.1` 始终表示“当前访问者自己的电脑”：

- 在服务器上访问时，它指向服务器；
- 在开发者电脑上访问时，它指向开发者电脑自己。

如果 `ROOT_URL` 使用 `127.0.0.1`，Gitea 网页可能生成其他电脑无法访问的仓库或 LFS 地址。

### 4.3 开放 Windows 防火墙

以管理员身份运行 PowerShell：

```powershell
New-NetFirewallRule `
    -DisplayName "Gitea LAN TCP 3000" `
    -Direction Inbound `
    -Protocol TCP `
    -LocalPort 3000 `
    -Action Allow `
    -Profile Private `
    -RemoteAddress LocalSubnet
```

该规则只允许本地子网访问，比直接向所有网络开放端口更合适。

### 4.4 检查端口

在服务器上检查监听进程：

```powershell
Get-NetTCPConnection -LocalPort 3000 -State Listen
```

检查对应进程：

```powershell
Get-NetTCPConnection -LocalPort 3000 -State Listen |
    Select-Object LocalAddress, LocalPort, OwningProcess,
        @{Name='Process'; Expression={(Get-Process -Id $_.OwningProcess).ProcessName}}
```

在其他局域网电脑上测试：

```powershell
Test-NetConnection 192.168.1.111 -Port 3000
```

成功结果应包含：

```text
TcpTestSucceeded : True
```

也可以直接访问：

```text
http://192.168.1.111:3000
```

## 5. 将现有 Unreal 仓库推送到 Gitea

### 5.1 创建空仓库

在 Gitea 中创建仓库时，不要初始化 README、`.gitignore` 或 License，避免与现有本地历史发生冲突。

当前示例仓库地址：

```text
http://192.168.1.111:3000/Blq/BrokenEdge.git
```

### 5.2 确认 Unreal LFS 规则

项目 `.gitattributes` 至少应覆盖关键二进制资产：

```gitattributes
*.uasset filter=lfs diff=lfs merge=lfs -text
*.umap filter=lfs diff=lfs merge=lfs -text
```

检查当前规则：

```powershell
git lfs track
```

检查具体文件是否由 LFS 管理：

```powershell
git check-attr filter -- Content/Maps/MainMap.umap
```

预期结果：

```text
Content/Maps/MainMap.umap: filter: lfs
```

### 5.3 添加远端并推送

```powershell
Set-Location C:\UnrealProjects\BrokenEdge

git remote add lan http://192.168.1.111:3000/Blq/BrokenEdge.git
git push -u lan --all
git push lan --tags
git lfs push --all lan
```

`git push --all` 推送 Git 分支，`git lfs push --all` 补齐所有本地可达历史中的 LFS 对象。对于从已有历史迁移到新 Gitea 的 Unreal 项目，不应省略最后一步。

检查远端：

```powershell
git remote -v
git branch -vv
git lfs status
```

## 6. 其他电脑克隆项目

客户端先安装 Git 和 Git LFS：

```powershell
git lfs install
git clone http://192.168.1.111:3000/Blq/BrokenEdge.git C:\test\BrokenEdge
```

进入仓库后检查：

```powershell
Set-Location C:\test\BrokenEdge
git status
git lfs status
git lfs ls-files
```

正常克隆必须同时满足：

- `Receiving objects` 和 `Resolving deltas` 完成；
- `Updating files` 完成；
- LFS 文件下载完成；
- 没有 `smudge filter lfs failed`；
- 工作区不是大量 `D` 与 `??` 并存。

## 7. 账号与仓库权限

### 7.1 三类管理员不要混淆

| 身份 | 范围 | 主要能力 |
| --- | --- | --- |
| 站点管理员 | 整个 Gitea | 管理用户、实例和所有仓库 |
| 仓库所有者 | 单个个人仓库 | 完整控制、删除或转让仓库 |
| 仓库 Admin 协作者 | 被授权仓库 | 管理仓库设置，但不能取代个人仓库所有者 |

个人仓库 URL 的第一段就是所有者：

```text
/Blq/BrokenEdge
 └─┬─┘
  所有者
```

个人仓库只有一个所有者。若需要多名所有者，应创建 Gitea 组织，并通过组织的 Owner Team 管理。

### 7.2 给其他账号推送权限

进入：

```text
仓库 → 设置 → 协作者
```

添加用户并授予：

```text
Write
```

权限含义：

| 权限 | 能力 |
| --- | --- |
| Read | 查看和克隆 |
| Write | 推送代码、创建和处理 Pull Request |
| Admin | 管理该仓库的设置和协作者 |

仓库地址中的 `Blq` 是仓库所有者，并不要求推送者使用 `Blq` 登录。每位开发者应使用自己的 Gitea 账号认证。

## 8. Pull Request 代码审核

### 8.1 推荐工作流

开发者不要直接改主开发分支，而是建立功能分支：

```powershell
git switch AfterCreateSession
git pull --ff-only
git switch -c feature/speed-pickup

# 修改并验证代码
git add --all
git commit -m "Add speed pickup"
git push -u origin feature/speed-pickup
```

随后在 Gitea 中创建 Pull Request：

```text
来源：feature/speed-pickup
目标：AfterCreateSession
```

审核者可以：

- 查看文本差异；
- 针对具体代码行评论；
- 批准修改；
- 请求修改；
- 在满足规则后合并。

### 8.2 配置受保护分支

进入：

```text
仓库 → 设置 → 分支 → 添加保护规则
```

对 `AfterCreateSession` 推荐设置：

```text
直接推送：禁用
所需批准数：1
拒绝审核时阻止合并：开启
新提交后取消旧批准：开启
管理员也必须遵守保护规则：开启
```

这会形成以下流程：

```text
功能分支
   │ push
   ▼
Pull Request
   │ 审核、评论、修改
   ▼
批准并合并
   │
   ▼
AfterCreateSession
```

### 8.3 Unreal 二进制资产的审核边界

Gitea 可以逐行审核 `.cpp`、`.h`、`.ini` 和 Markdown，但不能在网页中理解 `.uasset`、`.umap` 内部的 Blueprint 节点或关卡变化。

包含 Unreal 二进制资产的 Pull Request 应额外执行：

1. 将功能分支拉取到本地；
2. 用匹配版本的 Unreal Editor 打开；
3. 检查 Blueprint、地图、材质和引用是否正常；
4. 必要时运行多人 PIE 或独立进程测试；
5. 将验证结果写入 Pull Request 评论。

## 9. 日常协作命令

### 9.1 开始工作前

```powershell
git switch AfterCreateSession
git pull --ff-only
git lfs pull
```

### 9.2 创建功能分支

```powershell
git switch -c feature/<功能名称>
```

### 9.3 提交与推送

```powershell
git status --short
git add --all
git diff --cached --check
git lfs status
git commit -m "准确描述本次修改"
git push -u origin HEAD
```

### 9.4 合并后同步

```powershell
git switch AfterCreateSession
git pull --ff-only
git lfs pull
```

不要使用 `git push --force` 覆盖其他人的提交。确实需要改写历史时，应先获得仓库维护者确认。

## 10. 故障排查

### 10.1 `warning: redirecting to ...`

示例：

```text
warning: redirecting to http://192.168.1.111:3000/Blq/BrokenEdge.git/
```

常见原因是仓库改名或所有者发生转让。Git 能跟随重定向，但每次操作都可能继续警告。

查看当前地址：

```powershell
git remote -v
```

更新成 Gitea 网页显示的最新地址：

```powershell
git remote set-url origin http://192.168.1.111:3000/Blq/BrokenEdge.git
```

原始源仓库如果使用远端名 `lan`：

```powershell
git remote set-url lan http://192.168.1.111:3000/Blq/BrokenEdge.git
```

### 10.2 `Smudge error: batch response: Not Found`

典型输出：

```text
Error downloading object: Content/.../Asset.uasset (<OID>)
Smudge error: batch response: Not Found
error: external filter 'git-lfs filter-process' failed
warning: Clone succeeded, but checkout failed.
```

含义是：

1. Git 提交和 LFS 指针已经下载；
2. checkout 需要下载 LFS 真实内容；
3. Gitea 找不到指针引用的对象；
4. clone 的 Git 阶段成功，但工作区构建失败。

它通常不是 `.uasset` 损坏，也不是普通 Git 对象缺失。

#### 在源仓库确认对象

```powershell
git lfs ls-files --all --long | Select-String '<OID>'
```

Git LFS 本地对象通常位于：

```text
.git\lfs\objects\<OID前两位>\<OID第3至4位>\<完整OID>
```

#### 检查单个对象是否需要上传

```powershell
git lfs push --dry-run --object-id lan <OID>
```

如果输出 `push <OID> => <本地路径>`，说明本地具有该对象，并且它需要上传到远端。

#### 补传完整历史

```powershell
git lfs push --all lan
```

不要只上传报错中的第一个对象，否则 checkout 可能在下一个缺失对象处再次失败。

### 10.3 如何处理 checkout 失败的克隆目录

失败目录中可能同时出现大量：

```text
D  tracked-file
?? tracked-file
```

这是 checkout 中途失败留下的不完整状态，不代表服务器删除了这些文件。

先补齐服务器 LFS 对象，再保留失败目录作为备份：

```powershell
Rename-Item C:\test\BrokenEdge C:\test\BrokenEdge_failed
```

重新克隆：

```powershell
git clone http://192.168.1.111:3000/Blq/BrokenEdge.git C:\test\BrokenEdge
```

确认新克隆完整后，再决定是否删除 `_failed` 目录。

### 10.4 有 Write 权限但不能 push

依次检查：

1. 当前使用的是自己的 Gitea 账号；
2. 仓库协作者权限是否为 `Write`；
3. Windows 凭据管理器是否缓存了错误账号；
4. 目标分支是否为受保护分支；
5. 是否应该推送功能分支并创建 Pull Request。

查看当前跟踪关系：

```powershell
git remote -v
git branch -vv
```

如果目标分支受保护，使用：

```powershell
git switch -c feature/my-change
git push -u origin feature/my-change
```

### 10.5 服务器网页可访问但其他电脑不通

检查顺序：

1. Gitea 是否监听 `3000`；
2. 客户端访问的是否为 `192.168.1.111`，而不是 `127.0.0.1`；
3. Windows 网络配置文件是否为 `Private`；
4. 防火墙是否允许 `LocalSubnet` 访问 TCP 3000；
5. 路由器是否把服务器 IP 分配给了其他设备。

## 11. 服务器运行与备份建议

### 11.1 注册 Windows 服务

完成网页安装并生成 `app.ini` 后，可以按照 Gitea 官方 Windows 服务方式注册：

```powershell
sc.exe create gitea start= auto binPath= '"C:\gitea\gitea.exe" web --config "C:\gitea\custom\conf\app.ini"'
sc.exe start gitea
```

如果修改了运行账户，需要确保该账户对以下目录具有读写权限：

```text
C:\gitea\custom
C:\gitea\data
```

### 11.2 备份范围

至少备份：

```text
C:\gitea\custom\conf\app.ini
C:\gitea\data\gitea.db
C:\gitea\data\gitea-repositories
C:\gitea\data\lfs
```

只备份 Git 仓库而遗漏 `data\lfs`，恢复后仍会发生 LFS `404 Not Found`。

### 11.3 安全边界

当前 HTTP 方案只适合可信局域网：

- 不要把 TCP 3000 直接映射到公网；
- 公网访问应使用 VPN 或 HTTPS；
- 每个开发者使用独立账号；
- 不要共享管理员密码；
- 主分支使用保护规则和 Pull Request；
- 给普通开发者 `Write`，不要默认授予 `Admin`。

## 12. 验收清单

### 服务器

- [ ] `http://192.168.1.111:3000` 可从其他电脑访问；
- [ ] `ROOT_URL` 不是 `127.0.0.1`；
- [ ] `LFS_START_SERVER = true`；
- [ ] Gitea 数据库、Git 仓库和 LFS 目录可写；
- [ ] Windows 防火墙只对可信局域网放行 3000；
- [ ] 已规划 Gitea 数据目录备份。

### 仓库

- [ ] `.uasset` 和 `.umap` 由 Git LFS 管理；
- [ ] Git 分支和标签已经推送；
- [ ] 已执行 `git lfs push --all <remote>`；
- [ ] 仓库远端 URL 使用当前所有者；
- [ ] 开发者具有正确的 Read、Write 或 Admin 权限；
- [ ] 主要分支已经配置审核和保护规则。

### 客户端

- [ ] 已执行 `git lfs install`；
- [ ] clone 没有 `smudge filter lfs failed`；
- [ ] `git status` 没有因 checkout 中断产生的大量异常文件；
- [ ] Unreal Editor 能打开项目和 LFS 资产；
- [ ] 开发者能推送功能分支并创建 Pull Request。

## 13. 官方参考资料

- [Gitea 二进制安装](https://docs.gitea.com/installation/install-from-binary)
- [Gitea 注册为 Windows 服务](https://docs.gitea.com/installation/windows-service)
- [Gitea 权限模型](https://docs.gitea.com/usage/permissions)
- [Gitea Pull Request](https://docs.gitea.com/usage/issues-prs/pull-request)
- [Gitea 受保护分支](https://docs.gitea.com/usage/access-control/protected-branches)
- [Gitea Code Owners](https://docs.gitea.com/usage/repository/code-owners)
- [Git LFS 官方项目](https://git-lfs.com/)
