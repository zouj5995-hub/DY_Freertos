# Git 推送说明

## 远端信息

| 项 | 值 |
|---|---|
| 远端名 | **`DY_FreeRTOS`**（⚠️ 不是 `origin`） |
| 地址 | `https://github.com/zouj5995-hub/DY_Freertos.git` |
| 分支 | `main` |

## 正常推送

```powershell
git add -A
git commit -m "提交说明"
git push DY_FreeRTOS main
```

---

## ⚠️ 常见问题：推送失败（代理未运行）

### 现象

```
fatal: unable to access 'https://github.com/zouj5995-hub/DY_Freertos.git/':
Failed to connect to github.com:443 over proxy 127.0.0.1 after 2106 ms:
Could not connect to server
```

### 原因

全局 git 配置里设了代理：

```
git config --global --get http.proxy
→ http://127.0.0.1:7897          # Clash Verge 的默认端口
```

**当代理软件没有运行时，7897 端口无人监听**，git 仍按配置走代理，于是连接失败。

> 已实测：本机 **`github.com:443` 可以直连**，所以此时代理并不是必需的。

### 解决（二选一）

**方案 1：启动代理软件**（让 7897 可用），然后正常推送

```powershell
git push DY_FreeRTOS main
```

**方案 2：本次推送临时绕过代理**（不改动全局配置）

```powershell
git -c http.proxy= push DY_FreeRTOS main
```

> 只对本次命令生效，全局配置保持原样（代理开着时仍可正常用）。

### 如果确认不再需要代理

```powershell
git config --global --unset http.proxy     # 取消代理
git config --global --get http.proxy       # 确认已清空（无输出）
```

---

## ⚠️ 务必确认"真的推上去了"

**推送命令的输出可能被管道 `| Select-String ...` 过滤掉失败信息**，
（本项目曾因此误判为"推送成功"，实际有 17 个提交从未推上去）。

推送后**用下面任一方式复核**：

```powershell
# 方式 1：看是否还有 ahead
git status -sb
#   已同步   → ## main...DY_FreeRTOS/main
#   未同步   → ## main...DY_FreeRTOS/main [ahead 17]   ← 有 ahead 就是没推上去

# 方式 2：对比远端与本地的最新提交哈希是否一致
git -c http.proxy= ls-remote DY_FreeRTOS main
git log --oneline -1
```

**判断标准**：`git status -sb` 输出里**没有 `ahead`** 才算真正推送成功。
