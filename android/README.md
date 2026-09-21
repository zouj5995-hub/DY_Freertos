# DY LoRa Console Android

这是面向现场手机的原生 Android APK 工程。手机通过 OTG 连接 CH340 USB 转串口模块，应用直接使用 Android USB Host API 收发 LoRa 透传数据，不依赖 WebView、Web Serial 或在线第三方库。

## 已实现

- 自动发现 CH340/CH341（VID `1A86`）并请求 USB 权限。
- 固定串口参数：`9600 / 8N1 / 无校验 / 无流控`。
- `$READ`、`$GETSTR`、`$STR`、`$REST`、`$BDRMC` 协议帧。
- `$ACK` + `$STAR` 连续回包切分，支持 14 / 30 / 222 字节固定帧。
- Modbus CRC16：发送按协议大端；接收兼容当前固件小端 MCU 的历史 CRC 回包。
- 电压、三路温度、设备位、35 条策略、UTC 校时、日志和危险操作二次确认。
- 动画：USB 链路脉冲、收包状态反馈、指标刷新闪烁、策略行错峰入场；系统关闭动画时自动降级为静态界面。

## 用 Android Studio 构建 APK

1. 用 Android Studio 打开本目录 `android/`。
2. 等待 Gradle 同步，使用 JDK 17。
3. 选择 `app` 配置，执行 **Build → Build APK(s)**。
4. Debug APK 输出在：

```text
app/build/outputs/apk/debug/app-debug.apk
```

本仓库已附带 `build-apk.ps1`。在本工作区中，构建环境放在 `E:\AndroidBuild`，不会占用 C 盘；脚本会自动使用该目录下的 JDK/Gradle/Android SDK。

## 手机现场连接

1. 手机打开 OTG/USB Host（部分机型在系统设置里叫“OTG 连接”）。
2. 使用支持数据传输的 OTG 转接头连接 CH340 模块；如果模块供电不足，使用带供电的 OTG Hub。
3. 打开 APK，点击“连接 USB”。系统弹出 USB 权限确认时选择允许。
4. 确认 LoRa 两端均为 9600 / 8N1，再点击“读设备状态”。
5. 首次现场测试建议先用 `$READ`，确认电压、温度、时间和设备位均正确后，再读写策略或校时。

## CH340 兼容范围

当前内置驱动优先支持 CH340/CH341 的 Bulk IN/OUT 接口，常见 VID `1A86` 设备会自动识别。不同批次 CH340 的控制寄存器初始化可能存在差异；如果系统能授权但没有收到数据，请导出通信日志，并记录手机型号、Android 版本、CH340 VID/PID 和 OTG 供电情况。

## 安全和可用性

- 重启板子和下发整表策略均有二次确认。
- 未连接时发送动作会被拦截，并显示 OTG/USB 排查提示。
- 运行时不申请网络、定位、通讯录等无关权限。
- 日志只保留内存中的最近记录，不自动上传。
