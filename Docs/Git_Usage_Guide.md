# Git 使用指南：从本地提交到团队协作

## 1. 文档目标

本文面向需要在 Windows、C++ 或 Unreal Engine 项目中使用 Git 的开发者，目标不是只记住命令，而是理解 Git 的数据流和安全边界。

读完后应当能够：

- 判断修改当前位于工作区、暂存区还是提交历史；
- 正确执行检查、暂存、提交、拉取和推送；
- 使用分支隔离功能开发；
- 处理常见合并冲突；
- 根据修改所处阶段选择安全的撤销方法；
- 在 Unreal 项目中正确使用 Git LFS；
- 通过功能分支和 Pull Request 参与团队协作。

命令示例使用 PowerShell。除非特别说明，所有命令都应在仓库目录中执行。

## 2. Git 最重要的心智模型

### 2.1 四个位置

```text
工作区                 暂存区                  本地仓库                 远端仓库
Working Tree           Index/Stage            Local Repository         Remote
    │                       │                        │                      │
    │ git add               │                        │                      │
    ├──────────────────────▶│                        │                      │
    │                       │ git commit             │                      │
    │                       ├───────────────────────▶│                      │
    │                       │                        │ git push             │
    │                       │                        ├─────────────────────▶│
    │                       │                        │                      │
    │                       │                        │◀─────────────────────┤
    │                       │                        │ git fetch            │
    │◀──────────────────────────────────────────────┤                      │
                          git restore / checkout
```

四个位置分别回答不同问题：

| 位置 | 保存什么 | 常用检查命令 |
| --- | --- | --- |
| 工作区 | 当前磁盘上的文件 | `git status`、`git diff` |
| 暂存区 | 下一次提交准备记录的快照 | `git diff --cached` |
| 本地仓库 | 已完成的提交历史 | `git log`、`git show` |
| 远端仓库 | 团队共享的提交历史 | `git fetch`、`git remote -v` |

### 2.2 Git 保存的是快照

Git 提交不是“保存某个文件的一组编辑操作”，而是记录当时整个项目的一个快照。Git 会复用没有变化的内容，因此不需要担心每次提交都完整复制所有文件。

一次提交通常包含：

- 文件树快照；
- 提交作者和提交者；
- 时间；
- 提交说明；
- 父提交；
- 由这些内容计算得到的提交 ID。

这解释了为什么修改已经 `git add` 后，如果继续编辑同一文件，暂存区和工作区可以同时存在两个不同版本。

### 2.3 手动模拟一次提交

假设修改 `Player.cpp`：

```powershell
git status --short
```

输出：

```text
 M Source/Player.cpp
```

此时只有工作区改变。执行：

```powershell
git add Source/Player.cpp
```

状态变为：

```text
M  Source/Player.cpp
```

修改已经进入暂存区。随后再次编辑该文件，状态可能变为：

```text
MM Source/Player.cpp
```

第一个 `M` 表示暂存区有修改，第二个 `M` 表示暂存之后工作区又发生了修改。此时直接提交只会提交暂存区中的旧版本。

重新暂存最新磁盘内容：

```powershell
git add Source/Player.cpp
```

再执行：

```powershell
git commit -m "Update player movement"
```

## 3. 安装与首次配置

### 3.1 检查版本

```powershell
git --version
git lfs version
```

### 3.2 配置身份

```powershell
git config --global user.name "你的名字"
git config --global user.email "你的邮箱"
```

检查配置来源：

```powershell
git config --show-origin --get user.name
git config --show-origin --get user.email
```

`user.name` 和 `user.email` 会写入新提交。它们不是远端服务器的登录账号和密码。

### 3.3 初始化 Git LFS

每个 Windows 用户执行一次：

```powershell
git lfs install
```

仓库具体跟踪哪些扩展名由 `.gitattributes` 决定。

### 3.4 查看帮助

```powershell
git help commit
git commit --help
git commit -h
```

`--help` 通常打开完整文档，`-h` 显示简短参数说明。

## 4. 创建或获取仓库

### 4.1 初始化新仓库

```powershell
New-Item -ItemType Directory C:\Projects\MyProject
Set-Location C:\Projects\MyProject
git init
```

初始化后应先准备 `.gitignore` 和 `.gitattributes`，再提交项目文件。

### 4.2 克隆已有仓库

```powershell
git clone http://192.168.1.111:3000/Blq/BrokenEdge.git C:\Projects\BrokenEdge
```

克隆会完成：

1. 创建目标目录；
2. 下载 Git 对象；
3. 创建默认远端 `origin`；
4. 建立本地默认分支；
5. checkout 工作区；
6. 下载 checkout 所需的 Git LFS 对象。

指定分支克隆：

```powershell
git clone --branch AfterCreateSession `
    http://192.168.1.111:3000/Blq/BrokenEdge.git `
    C:\Projects\BrokenEdge
```

## 5. 查看状态与差异

### 5.1 `git status`

```powershell
git status
git status --short
git status --branch --short
```

短状态的两列分别表示暂存区和工作区：

```text
XY Path
```

常见状态：

| 输出 | 含义 |
| --- | --- |
| `?? File` | 未跟踪文件 |
| ` M File` | 工作区已修改，尚未暂存 |
| `M  File` | 修改已暂存 |
| `MM File` | 已暂存后，工作区又修改 |
| `A  File` | 新文件已暂存 |
| `D  File` | 删除已暂存 |
| `R  Old -> New` | 重命名已暂存 |
| `UU File` | 合并冲突未解决 |

### 5.2 查看未暂存差异

```powershell
git diff
git diff -- Source/BrokenEdge/Character/BrokenEdgeCharacter.cpp
```

### 5.3 查看已暂存差异

```powershell
git diff --cached
git diff --cached --name-status
git diff --cached --stat
git diff --cached --check
```

`git diff --cached --check` 可以发现行尾空白等问题，建议每次提交前执行。

### 5.4 查看提交内容

```powershell
git show HEAD
git show --stat HEAD
git show --name-status HEAD
```

## 6. 暂存与提交

### 6.1 暂存指定文件

```powershell
git add Source/Player.cpp
git add Source/Player.cpp Source/Player.h
```

### 6.2 暂存整个工作区

```powershell
git add --all
```

它会暂存新增、修改、删除和重命名。执行后必须检查：

```powershell
git diff --cached --name-status
git diff --cached --stat
git diff --cached --check
git status --short
```

### 6.3 交互式暂存

一个文件中包含多类修改时，可以按区块选择：

```powershell
git add --patch Source/Player.cpp
```

常用选择：

| 输入 | 含义 |
| --- | --- |
| `y` | 暂存当前区块 |
| `n` | 跳过当前区块 |
| `s` | 尝试拆分区块 |
| `q` | 退出 |

### 6.4 创建提交

```powershell
git commit -m "Add player sprint support"
```

好的提交说明通常：

- 使用明确动词；
- 描述做了什么，而不是“修改代码”；
- 一次提交聚焦一个完整目的；
- 不把临时调试和无关格式化混在一起。

示例：

```text
Add speed pickup buff
Fix session destroy and recreate flow
Document Gitea LAN setup
```

### 6.5 修改最后一次提交

最后一次提交尚未推送时，可以补充遗漏文件：

```powershell
git add MissingFile.cpp
git commit --amend --no-edit
```

修改提交说明：

```powershell
git commit --amend -m "New commit message"
```

`--amend` 会生成新的提交 ID。已经推送且被他人使用的提交不要随意 amend。

## 7. 提交历史

### 7.1 常用日志

```powershell
git log
git log --oneline --decorate --graph --all
git log --stat -5
git log -- Source/Player.cpp
```

推荐的紧凑视图：

```powershell
git log --graph --oneline --decorate --all -20
```

### 7.2 查看文件历史

```powershell
git log --follow -- Source/Player.cpp
git blame Source/Player.cpp
```

`git blame` 适合定位某行来自哪个提交，不应被当成追责工具。

### 7.3 比较两个提交或分支

```powershell
git diff <旧提交>..<新提交>
git diff main..feature/speed-pickup
git log main..feature/speed-pickup --oneline
```

## 8. 分支

### 8.1 分支是什么

Git 分支本质上是一个指向提交的可移动名称。创建分支并不会复制整个项目。

```text
A──B──C  main
       \
        D──E  feature/speed-pickup
```

### 8.2 查看分支

```powershell
git branch
git branch --all
git branch -vv
```

### 8.3 创建并切换分支

```powershell
git switch -c feature/speed-pickup
```

切换已有分支：

```powershell
git switch AfterCreateSession
```

### 8.4 重命名分支

```powershell
git branch -m new-branch-name
```

### 8.5 删除已合并本地分支

```powershell
git branch -d feature/speed-pickup
```

`-d` 会保护尚未合并的分支。`-D` 强制删除，应确认内容不再需要后再使用。

## 9. 远端仓库

### 9.1 查看远端

```powershell
git remote -v
git remote show origin
```

### 9.2 添加远端

```powershell
git remote add origin http://server/user/repository.git
```

一个仓库可以有多个远端，例如：

```powershell
git remote add lan http://192.168.1.111:3000/Blq/BrokenEdge.git
git remote add backup https://example.com/Blq/BrokenEdge.git
```

### 9.3 修改远端地址

```powershell
git remote set-url origin http://new-server/user/repository.git
```

仓库转让或改名后出现重定向警告时，应更新为网页显示的最新地址。

### 9.4 远端跟踪分支

```text
AfterCreateSession             本地分支
origin/AfterCreateSession      本地记录的远端状态
```

`origin/AfterCreateSession` 不是服务器上的实时对象。只有执行 `git fetch` 后，它才更新为最近获取的远端状态。

## 10. 获取、拉取与推送

### 10.1 `git fetch`

```powershell
git fetch origin
git fetch --all --prune
```

`fetch` 只更新远端跟踪分支，不自动修改当前工作区，适合先检查再决定如何整合。

```powershell
git log --oneline HEAD..origin/AfterCreateSession
git diff HEAD..origin/AfterCreateSession
```

### 10.2 `git pull`

```powershell
git pull --ff-only
```

`pull` 可以理解为先 `fetch`，再把远端变化整合进当前分支。

推荐使用 `--ff-only`：如果无法快进，它会停止，让开发者明确选择 merge 或 rebase，而不是意外生成合并提交。

### 10.3 `git push`

首次推送当前分支并设置跟踪关系：

```powershell
git push -u origin HEAD
```

以后可以直接：

```powershell
git push
```

推送指定分支：

```powershell
git push origin feature/speed-pickup
```

### 10.4 非快进拒绝

典型错误：

```text
rejected (non-fast-forward)
```

说明远端包含本地尚未整合的提交。先执行：

```powershell
git fetch origin
git log --graph --oneline --decorate --all -20
```

再根据团队策略 merge 或 rebase。不要把 `git push --force` 当成默认解决方案。

## 11. Merge 与 Rebase

### 11.1 Merge

```powershell
git switch AfterCreateSession
git merge feature/speed-pickup
```

Merge 保留真实分叉关系，必要时产生一个合并提交：

```text
A──B─────M
    \   /
     C─D
```

适合：

- 合并已经共享的分支；
- 希望保留分支历史；
- 通过 Gitea Pull Request 合并。

### 11.2 Rebase

在自己的功能分支上更新基线：

```powershell
git switch feature/speed-pickup
git fetch origin
git rebase origin/AfterCreateSession
```

Rebase 会重放提交并生成新的提交 ID：

```text
变基前：A──B──C  main
             \
              D──E  feature

变基后：A──B──C──D'──E'  feature
```

适合：

- 尚未共享或只由自己使用的功能分支；
- 希望提交历史保持线性。

已经由多人共同使用的分支不要随意 rebase。

## 12. 冲突处理

### 12.1 冲突为什么发生

如果两个分支修改了同一文件的同一区域，Git 无法自动决定保留哪一方，会停止并要求人工处理。

冲突标记：

```text
<<<<<<< HEAD
当前分支内容
=======
另一分支内容
>>>>>>> feature/example
```

开发者必须理解两边意图，编辑成最终正确内容，并删除冲突标记。

### 12.2 处理 merge 冲突

```powershell
git status

# 编辑冲突文件并验证
git add <已解决文件>
git commit
```

放弃本次 merge：

```powershell
git merge --abort
```

### 12.3 处理 rebase 冲突

```powershell
git status

# 编辑并暂存冲突文件
git add <已解决文件>
git rebase --continue
```

放弃本次 rebase：

```powershell
git rebase --abort
```

### 12.4 Unreal 二进制冲突

`.uasset` 和 `.umap` 无法像文本一样手工合并冲突。通常需要：

1. 决定保留哪一方资产；
2. 在 Unreal Editor 中重新应用另一方需要的修改；
3. 保存资产；
4. 重新暂存并验证。

因此团队应尽量避免多人同时修改同一 Blueprint 或地图，并通过任务拆分和功能分支减少冲突。

## 13. 撤销与恢复

撤销前先回答两个问题：

1. 修改位于工作区、暂存区、本地提交还是远端？
2. 这些内容是否已经共享给其他人？

### 13.1 放弃未暂存修改

```powershell
git restore Source/Player.cpp
```

该命令会用暂存区版本覆盖工作区文件。未提交修改会丢失，执行前应检查 `git diff`。

### 13.2 取消暂存但保留文件内容

```powershell
git restore --staged Source/Player.cpp
```

这只把修改移出暂存区，不会丢失工作区编辑。

### 13.3 撤销已经共享的提交

```powershell
git revert <commit-id>
```

`revert` 会创建一个反向提交，不改写已有历史，适合撤销已经推送的提交。

### 13.4 找回移动过的 HEAD

```powershell
git reflog
```

`reflog` 记录本地引用的移动历史。误操作后应先停止继续改写仓库，再用它查找原提交 ID。

### 13.5 高风险命令

以下命令可能直接丢弃工作区、未跟踪文件或改写历史：

```text
git reset --hard
git clean -fd
git push --force
git branch -D
```

运行前必须确认精确目标，并确保需要的内容已提交、备份或不再需要。团队分支若确实需要强制推送，应优先考虑：

```powershell
git push --force-with-lease
```

它仍会改写远端历史，但会在远端发生意外变化时拒绝覆盖。

## 14. 临时保存工作区

### 14.1 创建 stash

```powershell
git stash push -m "WIP speed pickup"
```

同时包含未跟踪文件：

```powershell
git stash push --include-untracked -m "WIP speed pickup"
```

### 14.2 查看和恢复

```powershell
git stash list
git stash show --patch 'stash@{0}'
git stash apply 'stash@{0}'
```

确认恢复正确后再删除：

```powershell
git stash drop 'stash@{0}'
```

`git stash pop` 会应用后尝试立即删除记录。重要工作更适合先 `apply`、验证，再 `drop`。

## 15. 标签

创建带说明标签：

```powershell
git tag -a v1.0.0 -m "BrokenEdge v1.0.0"
```

查看：

```powershell
git tag
git show v1.0.0
```

推送标签：

```powershell
git push origin v1.0.0
git push origin --tags
```

标签适合标记发布版本或重要里程碑，不应被当成普通可移动分支。

## 16. `.gitignore` 与 `.gitattributes`

### 16.1 `.gitignore`

`.gitignore` 只影响尚未跟踪的文件。已经提交过的文件不会因为后来加入忽略规则而自动停止跟踪。

Unreal 常见生成目录：

```gitignore
Binaries/
DerivedDataCache/
Intermediate/
Saved/
.vs/
```

不要忽略：

```text
Config/
Content/
Plugins/ 中的项目插件源代码和内容
Source/
*.uproject
```

检查某个路径为何被忽略：

```powershell
git check-ignore -v Saved/Logs/Project.log
```

### 16.2 `.gitattributes`

`.gitattributes` 可以控制行尾、差异方式和 Git LFS：

```gitattributes
*.uasset filter=lfs diff=lfs merge=lfs -text
*.umap filter=lfs diff=lfs merge=lfs -text
*.fbx filter=lfs diff=lfs merge=lfs -text
*.wav filter=lfs diff=lfs merge=lfs -text
```

`.gitattributes` 应当提交到仓库，让所有开发者使用一致规则。

## 17. Git LFS

### 17.1 工作原理

Git 提交中保存的是类似下面的指针：

```text
version https://git-lfs.github.com/spec/v1
oid sha256:<对象ID>
size <字节数>
```

真实二进制内容保存在本地 `.git/lfs/objects` 和远端 LFS 服务中。

### 17.2 查看 LFS 文件

```powershell
git lfs track
git lfs ls-files
git lfs status
```

查看所有历史引用：

```powershell
git lfs ls-files --all --long
```

### 17.3 推送完整 LFS 历史

把已有 Unreal 仓库迁移到新远端时：

```powershell
git push -u origin --all
git push origin --tags
git lfs push --all origin
```

只推送 Git 分支而没有上传 LFS 对象，会导致其他电脑 clone 时出现：

```text
Smudge error
batch response: Not Found
Clone succeeded, but checkout failed
```

### 17.4 已有大文件迁移到 LFS

仅运行 `git lfs track` 不会自动改写历史中的大文件。`git lfs migrate import` 会重写提交历史，因此共享仓库中必须先与所有协作者协调，不应独自执行。

## 18. Pull Request 团队工作流

推荐流程：

```text
同步主要分支
      │
      ▼
创建 feature 分支
      │
      ▼
修改、验证、提交
      │
      ▼
推送 feature 分支
      │
      ▼
创建 Pull Request
      │
      ▼
审核 / 请求修改 / 批准
      │
      ▼
合并到主要分支
```

命令示例：

```powershell
git switch AfterCreateSession
git pull --ff-only
git switch -c feature/speed-pickup

# 完成功能并验证
git add --all
git diff --cached --check
git lfs status
git commit -m "Add speed pickup buff"
git push -u origin HEAD
```

在 Gitea 创建 Pull Request 后，审核者检查：

- 修改目的是否明确；
- 文本差异是否正确；
- 是否包含意外文件；
- 是否存在需要拆分的无关修改；
- 构建和运行时验证是否充分；
- `.uasset/.umap` 是否已在 Unreal Editor 中验证。

## 19. Unreal 项目提交清单

### 19.1 提交前

```powershell
git status --short
git diff -- Source Config Plugins Docs
git add --all
git diff --cached --name-status
git diff --cached --stat
git diff --cached --check
git lfs status
```

确认：

- `Binaries/`、`Intermediate/`、`Saved/` 没有进入提交；
- `.uasset` 和 `.umap` 以 LFS 对象形式暂存；
- Blueprint、地图和代码修改都在预期范围；
- Unreal Editor 没有正在保存资产；
- 没有把本地账号、令牌或机器专属配置提交进去。

### 19.2 提交后

```powershell
git status --short
git show --stat HEAD
git lfs status
```

提交成功不等于编译或运行时验证成功。交付说明应明确区分：

| 验证 | 能证明什么 |
| --- | --- |
| `git diff --check` | 没有被该检查识别的空白错误 |
| Git/LFS 状态干净 | 修改已经被提交，没有遗留工作区变化 |
| UBT/UHT 编译成功 | 当前目标通过代码生成、编译和链接 |
| Unreal Editor 测试 | 资产加载和实际玩法行为通过验证 |

## 20. 常见问题

### 20.1 `nothing to commit, working tree clean`

说明当前工作区和暂存区相对 `HEAD` 没有变化。它不表示本地一定已经推送到远端。

检查领先或落后：

```powershell
git status --branch --short
git branch -vv
```

### 20.2 `pathspec ... did not match`

常见原因：

- 当前目录不是预期仓库；
- 路径拼写错误；
- 分支名不存在；
- 文件尚未被 Git 识别。

检查：

```powershell
git rev-parse --show-toplevel
git status --short
git branch --all
```

### 20.3 `detected dubious ownership`

Git 发现仓库目录所有者与当前用户不匹配。不要直接把整个磁盘加入安全目录，应先确认路径确实可信，再只添加具体仓库：

```powershell
git config --global --add safe.directory C:/Exact/Trusted/Repository
```

### 20.4 凭据使用了错误账号

HTTP 远端可能使用 Windows 凭据管理器缓存账号。先确认远端 URL，再在 Windows“凭据管理器”中删除对应服务器的错误 Git 凭据，然后重新 push 并输入正确账号。

### 20.5 文件行尾反复变化

Windows 和其他平台可能使用不同换行符。优先通过仓库 `.gitattributes` 统一规则，不要在没有评估的情况下批量重新规范化整个项目。

检查全局配置来源：

```powershell
git config --show-origin --get core.autocrlf
```

## 21. 每日推荐流程

```powershell
# 1. 进入仓库并检查状态
Set-Location C:\UnrealProjects\BrokenEdge
git status --branch --short

# 2. 同步主要分支
git switch AfterCreateSession
git pull --ff-only
git lfs pull

# 3. 创建功能分支
git switch -c feature/my-feature

# 4. 修改并验证项目

# 5. 审计并提交
git status --short
git add --all
git diff --cached --name-status
git diff --cached --check
git lfs status
git commit -m "Describe the completed change"

# 6. 推送功能分支
git push -u origin HEAD

# 7. 在 Gitea 创建 Pull Request
```

## 22. 命令速查

| 目的 | 命令 |
| --- | --- |
| 查看仓库根目录 | `git rev-parse --show-toplevel` |
| 查看状态 | `git status --branch --short` |
| 查看工作区差异 | `git diff` |
| 查看暂存差异 | `git diff --cached` |
| 暂存全部修改 | `git add --all` |
| 检查空白错误 | `git diff --cached --check` |
| 创建提交 | `git commit -m "message"` |
| 查看图形日志 | `git log --graph --oneline --decorate --all` |
| 创建功能分支 | `git switch -c feature/name` |
| 切换分支 | `git switch branch-name` |
| 获取远端状态 | `git fetch --all --prune` |
| 只允许快进拉取 | `git pull --ff-only` |
| 首次推送当前分支 | `git push -u origin HEAD` |
| 取消暂存 | `git restore --staged <path>` |
| 放弃未暂存修改 | `git restore <path>` |
| 安全撤销共享提交 | `git revert <commit>` |
| 查看引用移动记录 | `git reflog` |
| 查看 LFS 状态 | `git lfs status` |
| 上传全部 LFS 历史 | `git lfs push --all origin` |

## 23. 练习

### 练习一：判断修改所在位置

依次执行：

```powershell
git status --short
git add <某个文件>
git status --short
```

然后再次编辑同一文件，观察 `MM` 状态。分别使用 `git diff` 和 `git diff --cached`，说明两份差异为什么不同。

### 练习二：安全撤销暂存

1. 修改一个测试文件；
2. 执行 `git add`；
3. 使用 `git restore --staged`；
4. 验证磁盘内容仍然存在；
5. 再次暂存并提交。

### 练习三：模拟两个分支

1. 从主要分支创建 `feature/a`；
2. 创建并提交一个文本文件；
3. 切回主要分支；
4. 比较两个分支；
5. 合并 `feature/a`；
6. 用 `git log --graph --oneline --decorate --all` 观察提交图。

### 练习四：推导 LFS 失败原因

回答：为什么普通 Git 对象已经 100% 下载，仍可能出现 `Clone succeeded, but checkout failed`？

提示：分别说明 Git 提交中的 LFS 指针、远端 LFS 对象和 checkout 三者的关系。

## 24. 需要牢记的原则

1. 先 `status`，再执行改变状态的命令。
2. `git add` 选择的是下一次提交的快照，不等于备份到远端。
3. 提交前检查暂存区，而不是只看工作区。
4. `fetch` 负责获取，`merge/rebase` 负责整合，`pull` 是组合操作。
5. 已经共享的历史优先用 `revert` 撤销。
6. 不要用 `push --force` 掩盖没有理解的分支问题。
7. Unreal 二进制资产必须同时满足 Git 指针和 LFS 对象完整。
8. Pull Request 审核不能替代 Unreal Editor 对二进制资产的验证。
9. 编译成功、运行成功、提交成功和推送成功是四个不同结论。
10. 任何可能丢失数据的命令，执行前都要确认精确目标和恢复路径。

## 25. 延伸阅读

- [Git 官方参考手册](https://git-scm.com/docs)
- [Pro Git 中文版](https://git-scm.com/book/zh/v2)
- [Git LFS 官方网站](https://git-lfs.com/)
- [本项目的 Windows 局域网 Gitea 与 Unreal Git LFS 协作指南](Gitea_LAN_Git_Server_Guide.md)
