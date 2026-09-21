# DY LoRa Android Motion Spec

## Concept

**链路脉冲（Link Pulse）**：把 LoRa/CH340 的“字节流正在发生”翻译成一个不遮挡数据的微型现场信号。动画不是装饰，而是给连接、收包和策略回读提供时间反馈。

## Arc

单屏仪器（single-screen instrument）：手机上位机的主要动作不是滚动，而是连接、读取、编辑、发送。动效集中在状态边界和操作反馈，静态界面在关闭动画时仍完整可用。

## Motion decisions

1. **USB 在线脉冲**：连接后以低幅度同心波纹持续呼吸；未连接时完全静止，并显示“等待 OTG / CH340 串口授权”。
2. **收包闪烁**：`$ACK` / `$STAR` / `$STR` 到达时，只对对应指标或策略行做 80ms → 260ms 的透明度反馈，不移动布局。
3. **策略错峰**：35 行策略以最多 420ms 的渐进延迟入场，避免一次性闪现；系统关闭动画时直接呈现最终状态。

## Reduced motion

遵循系统动画开关：`ValueAnimator.areAnimatorsEnabled()` 为 false 时跳过脉冲、闪烁和错峰，只保留功能和可读状态。

## Verification targets

- OTG 授权后连接状态必须在一秒内变为“已连接”。
- `$READ` 连续回包必须能切分 `$ACK` 14 字节和 `$STAR` 30 字节。
- 发送帧与桌面版示例 CRC 保持一致。
- Android 8.0+ 竖屏 360dp～600dp 宽度下不出现关键按钮裁切。
