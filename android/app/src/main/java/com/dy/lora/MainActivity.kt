package com.dy.lora

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.animation.DecelerateInterpolator
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import java.time.LocalDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale
import kotlin.math.min

class MainActivity : Activity(), UsbSerialManager.Listener {
    private val mainHandler = Handler(Looper.getMainLooper())
    private val parser = ProtocolCodec.StreamParser()
    private val rules = MutableList(35) { StrategyRule() }
    private val history = ArrayDeque<StarStatus>()
    private lateinit var usb: UsbSerialManager
    private lateinit var linkPulse: LinkPulseView
    private lateinit var connectionLabel: TextView
    private lateinit var portLabel: TextView
    private lateinit var boatInput: EditText
    private lateinit var statusTime: TextView
    private lateinit var voltageValue: TextView
    private lateinit var ntc1Value: TextView
    private lateinit var ntc2Value: TextView
    private lateinit var pcbValue: TextView
    private lateinit var statusSummary: TextView
    private lateinit var deviceContainer: LinearLayout
    private lateinit var rulesContainer: LinearLayout
    private lateinit var logContainer: LinearLayout
    private lateinit var timeInput: EditText
    private var connected = false
    private var pollMs = 0L
    private var pollRunnable: Runnable? = null
    private val gatedButtons = mutableListOf<Button>()

    private val displayDate = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss", Locale.US)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = color(R.color.dy_bg)
        window.navigationBarColor = color(R.color.dy_bg)
        usb = UsbSerialManager(this, this)
        usb.register()
        setContentView(buildContent())
        setConnected(false, "未连接")
        renderRules()
        timeInput.setText(displayDate.format(LocalDateTime.now()))
    }

    override fun onDestroy() {
        stopPolling()
        usb.unregister()
        super.onDestroy()
    }

    override fun onConnectionState(isConnected: Boolean, label: String) {
        runOnUiThread { setConnected(isConnected, label) }
    }

    override fun onBytesReceived(bytes: ByteArray) {
        runOnUiThread {
            parser.append(bytes).forEach { frame ->
                if (!frame.crcValid) {
                    addLog("RX", frame.bytes, frame.error ?: "无效帧", true)
                    toast("收到 CRC 错误帧")
                    return@forEach
                }
                val legacy = if (frame.legacyLittleEndianCrc) " · 兼容固件 CRC 小端" else ""
                when (frame.type) {
                    ParsedFrame.Type.ACK -> {
                        val ack = frame.ack ?: return@forEach
                        addLog("RX", frame.bytes, "\$ACK · ${if (ack.error == 0) "成功" else "失败 error=${ack.error}"}$legacy", ack.error != 0)
                        toast(if (ack.error == 0) "设备已确认命令" else "设备拒绝命令")
                    }
                    ParsedFrame.Type.STAR -> {
                        val star = frame.star ?: return@forEach
                        addLog("RX", frame.bytes, "\$STAR · ${"%.1f".format(star.voltage)} V · PCB ${"%.1f".format(star.pcb)} ℃$legacy")
                        applyStar(star)
                    }
                    ParsedFrame.Type.STR -> {
                        frame.rules?.forEachIndexed { index, rule -> rules[index] = rule }
                        renderRules()
                        addLog("RX", frame.bytes, "\$STR · 35 条规则已回读$legacy")
                        toast("控制策略回读完成")
                    }
                    ParsedFrame.Type.UNKNOWN -> addLog("RX", frame.bytes, "未知帧头$legacy", true)
                }
            }
        }
    }

    override fun onTransportLog(message: String, error: Boolean) {
        runOnUiThread {
            addLog("SYS", byteArrayOf(), message, error)
            if (error) toast(message)
        }
    }

    private fun buildContent(): View {
        val scroll = ScrollView(this).apply { setBackgroundColor(color(R.color.dy_bg)); isFillViewport = true }
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(16), dp(12), dp(16), dp(28)) }
        root.addView(buildHeader())
        root.addView(buildIntro())
        root.addView(buildTelemetryPanel())
        root.addView(buildCommandPanel())
        root.addView(buildTimePanel())
        root.addView(buildRulesPanel())
        root.addView(buildLogPanel())
        val footer = text("DY LoRa Console · OTG / CH340 · 9600 / 8N1", 11f, R.color.dy_faint).apply { setPadding(0, dp(18), 0, 0) }
        root.addView(footer)
        scroll.addView(root)
        return scroll
    }

    private fun buildHeader(): View {
        val header = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(0), dp(4), dp(0), dp(12))
        }
        val titleRow = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL }
        val mark = text("DY", 17f, R.color.dy_accent).apply { gravity = Gravity.CENTER; background = rounded(R.color.dy_bg, R.color.dy_accent, 1, 7); setTypeface(typeface, android.graphics.Typeface.BOLD) }
        titleRow.addView(mark, LinearLayout.LayoutParams(dp(40), dp(40)))
        val titleStack = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(10), 0, 0, 0) }
        titleStack.addView(text("DY POWER / LORA CONSOLE", 16f, R.color.dy_text).apply { setTypeface(typeface, android.graphics.Typeface.BOLD) })
        titleStack.addView(text("安卓 OTG 调试台 · CH340 / 9600 8N1", 11f, R.color.dy_muted))
        titleRow.addView(titleStack, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        header.addView(titleRow)

        connectionLabel = text("未连接", 12f, R.color.dy_muted).apply {
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(12), 0, dp(12), 0)
            background = rounded(R.color.dy_surface_2, R.color.dy_line, 1, 6)
        }
        header.addView(connectionLabel, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(38)).apply { setMargins(0, dp(10), 0, 0) })

        val controls = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL; setPadding(dp(0), dp(8), dp(0), 0) }
        controls.addView(text("船号", 12f, R.color.dy_muted))
        boatInput = EditText(this).apply { setText("1"); setTextColor(color(R.color.dy_text)); textSize = 14f; inputType = InputType.TYPE_CLASS_NUMBER; setSingleLine(); gravity = Gravity.CENTER; background = rounded(R.color.dy_surface_2, R.color.dy_line, 1, 6); setPadding(dp(8), 0, dp(8), 0) }
        controls.addView(boatInput, LinearLayout.LayoutParams(dp(54), dp(42)).apply { setMargins(dp(7), 0, dp(10), 0) })
        val connect = button("连接 USB", R.color.dy_accent) { usb.requestFirstSupportedDevice() }
        controls.addView(connect, LinearLayout.LayoutParams(0, dp(42), 1f))
        val disconnect = button("断开", R.color.dy_surface_3) { usb.close() }
        disconnect.tag = "disconnect"
        controls.addView(disconnect, LinearLayout.LayoutParams(dp(76), dp(42)).apply { setMargins(dp(8), 0, 0, 0) })
        header.addView(controls)
        return header
    }

    private fun buildIntro(): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(0, dp(10), 0, dp(18)) }
        box.addView(text("现场维护工作台 / ANDROID", 11f, R.color.dy_accent).apply { letterSpacing = .12f })
        box.addView(text("让每一帧状态，都能被看见。", 30f, R.color.dy_text).apply { setTypeface(typeface, android.graphics.Typeface.BOLD); setPadding(0, dp(5), 0, dp(5)) })
        box.addView(text("手机通过 OTG 直连 CH340，原样收发 LoRa 协议帧。动画只强调链路状态，不遮挡现场数据。", 13f, R.color.dy_muted))
        return box
    }

    private fun buildTelemetryPanel(): View {
        val body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        linkPulse = LinkPulseView(this)
        body.addView(linkPulse, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(80)))
        val metrics = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val row1 = LinearLayout(this).apply { weightSum = 2f }
        statusTime = metric(row1, "设备时间", "—", 1f)
        statusTime.textSize = 13f
        voltageValue = metric(row1, "电池电压", "— V", 1f)
        metrics.addView(row1)
        val row2 = LinearLayout(this).apply { weightSum = 2f }
        ntc1Value = metric(row2, "环境 / NTC1", "— ℃", 1f)
        ntc2Value = metric(row2, "鳍片 / NTC2", "— ℃", 1f)
        metrics.addView(row2)
        body.addView(metrics)
        pcbValue = text("电源板温度 — ℃", 15f, R.color.dy_teal).apply { setPadding(dp(12), dp(12), dp(12), dp(4)); setTypeface(typeface, android.graphics.Typeface.BOLD) }
        body.addView(pcbValue)
        statusSummary = text("等待 \$STAR 状态帧", 11f, R.color.dy_muted).apply { setPadding(dp(12), 0, dp(12), dp(12)) }
        body.addView(statusSummary)
        deviceContainer = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(0), dp(4), dp(0), dp(2)) }
        body.addView(deviceContainer)
        renderDevices(0)
        return panel("TELEMETRY / \$STAR", "设备状态", "最近收到的状态上报", body)
    }

    private fun buildCommandPanel(): View {
        val body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val row1 = LinearLayout(this).apply { weightSum = 2f }
        row1.addView(gated(button("读设备状态", R.color.dy_accent) { sendRead() }), LinearLayout.LayoutParams(0, dp(44), 1f).apply { setMargins(0, 0, dp(5), dp(5)) })
        row1.addView(gated(button("读控制策略", R.color.dy_surface_3) { sendGetStrategy() }), LinearLayout.LayoutParams(0, dp(44), 1f).apply { setMargins(dp(5), 0, 0, dp(5)) })
        body.addView(row1)
        val row2 = LinearLayout(this).apply { weightSum = 2f }
        row2.addView(gated(button("下发控制策略", R.color.dy_surface_3) { confirm("确认下发控制策略？", "这会整表覆盖设备内 35 条规则。", ::sendStrategy) }), LinearLayout.LayoutParams(0, dp(44), 1f).apply { setMargins(0, 0, dp(5), dp(5)) })
        row2.addView(gated(button("重启板子", R.color.dy_surface_3) { confirm("确认重启板子？", "设备将在约 300 ms 后复位，当前链路会中断。", ::sendRestart) }), LinearLayout.LayoutParams(0, dp(44), 1f).apply { setMargins(dp(5), 0, 0, dp(5)) })
        body.addView(row2)
        val pollRow = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL; setPadding(0, dp(10), 0, 0) }
        pollRow.addView(text("自动轮询", 12f, R.color.dy_muted))
        val pollButton = button("关闭", R.color.dy_surface_2) { cyclePolling(it as Button) }
        pollButton.tag = "poll"
        pollRow.addView(pollButton, LinearLayout.LayoutParams(dp(110), dp(38)).apply { setMargins(dp(10), 0, 0, 0) })
        pollRow.addView(text("轻触切换 2 / 5 / 10 / 30 秒", 11f, R.color.dy_faint).apply { setPadding(dp(10), 0, 0, 0) })
        body.addView(pollRow)
        return panel("COMMAND DECK", "命令面板", "命令和应答都会写入通信日志", body)
    }

    private fun buildTimePanel(): View {
        val body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        timeInput = EditText(this).apply { setTextColor(color(R.color.dy_text)); textSize = 14f; setSingleLine(); hint = "yyyy-MM-dd HH:mm:ss"; background = rounded(R.color.dy_surface_2, R.color.dy_line, 1, 6); setPadding(dp(10), 0, dp(10), 0) }
        body.addView(timeInput, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)))
        val row = LinearLayout(this).apply { setPadding(0, dp(8), 0, 0) }
        row.addView(button("使用电脑时间", R.color.dy_surface_3) { timeInput.setText(displayDate.format(LocalDateTime.now())); toast("已填入电脑当前时间") }, LinearLayout.LayoutParams(0, dp(42), 1f).apply { setMargins(0, 0, dp(5), 0) })
        row.addView(gated(button("发送校时", R.color.dy_accent) { sendTime() }), LinearLayout.LayoutParams(0, dp(42), 1f).apply { setMargins(dp(5), 0, 0, 0) })
        body.addView(row)
        body.addView(text("发送的是 UTC 时间（北京时间 − 8 小时）。无 ACK 属正常，发送后重新读取状态确认。", 11f, R.color.dy_accent).apply { setPadding(0, dp(10), 0, 0) })
        return panel("CLOCK / \$BDRMC", "北斗校时", "手机本地时间会在发送前自动换算 UTC", body)
    }

    private fun buildRulesPanel(): View {
        rulesContainer = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        body.addView(text("填写开始与结束时间，协议持续秒数自动计算。结束早于开始表示次日结束。", 11f, R.color.dy_muted).apply { setPadding(0, 0, 0, dp(10)) })
        body.addView(rulesContainer, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        return panel("STRATEGY / \$STR", "控制策略 · 35 条", "开始时间 + 结束时间；发送时自动换算协议持续秒数", body)
    }

    private fun buildLogPanel(): View {
        logContainer = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(0, 0, 0, dp(2)) }
        val body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val clear = button("清空日志", R.color.dy_surface_3) { logContainer.removeAllViews() }
        body.addView(clear, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(40)))
        body.addView(logContainer, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(360)))
        return panel("WIRE LOG / RAW BYTES", "通信日志", "最多保留 1000 条，错误帧使用红色标记", body)
    }

    private fun renderRules() {
        if (!::rulesContainer.isInitialized) return
        rulesContainer.removeAllViews()
        rules.forEachIndexed { index, rule ->
            val card = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(10), dp(9), dp(10), dp(10))
                background = rounded(if (index % 2 == 0) R.color.dy_surface_2 else R.color.dy_surface_3, R.color.dy_line, 1, 6)
            }
            var durationText: TextView? = null
            fun changed(update: (Int) -> Unit) = { value: Int -> update(value); syncRuleDuration(rule); durationText?.text = formatDuration(rule.duration) }

            val head = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL }
            head.addView(text("规则 %02d".format(index + 1), 13f, R.color.dy_text).apply { setTypeface(typeface, android.graphics.Typeface.BOLD) }, LinearLayout.LayoutParams(0, dp(42), 1f))
            val labels = ProtocolCodec.targets.map { it.second }
            val spinner = Spinner(this).apply {
                adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, labels)
                setSelection(ProtocolCodec.targets.indexOfFirst { it.first == rule.target }.coerceAtLeast(0))
                setOnItemSelectedListener(object : android.widget.AdapterView.OnItemSelectedListener {
                    override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
                    override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) { rule.target = ProtocolCodec.targets[position].first }
                })
            }
            head.addView(spinner, LinearLayout.LayoutParams(0, dp(42), 2f))
            card.addView(head)

            card.addView(ruleTimeRow("开始", rule.hour, rule.minute, rule.second, changed { rule.hour = it }, changed { rule.minute = it }, changed { rule.second = it }))
            card.addView(ruleTimeRow("结束", rule.endHour, rule.endMinute, rule.endSecond, changed { rule.endHour = it }, changed { rule.endMinute = it }, changed { rule.endSecond = it }))
            durationText = text(formatDuration(rule.duration), 11f, R.color.dy_muted).apply { setPadding(dp(50), dp(5), 0, 0) }
            card.addView(durationText)
            rulesContainer.addView(card, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 0, 0, dp(8)) })
            animateIn(card, min(index * 14L, 420L))
        }
    }

    private fun ruleTimeRow(label: String, hour: Int, minute: Int, second: Int, onHour: (Int) -> Unit, onMinute: (Int) -> Unit, onSecond: (Int) -> Unit): View {
        val row = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL; setPadding(0, dp(6), 0, 0) }
        row.addView(text(label, 11f, R.color.dy_muted).apply { gravity = Gravity.CENTER_VERTICAL }, LinearLayout.LayoutParams(dp(42), dp(40)))
        fun field(value: Int, max: Int, onChange: (Int) -> Unit) {
            val holder = LinearLayout(this).apply { gravity = Gravity.CENTER }
            addRuleInput(holder, value, 0, max, onChange)
            row.addView(holder, LinearLayout.LayoutParams(0, dp(40), 1f).apply { setMargins(dp(3), 0, dp(3), 0) })
        }
        field(hour, 23, onHour)
        row.addView(text(":", 14f, R.color.dy_faint).apply { gravity = Gravity.CENTER }, LinearLayout.LayoutParams(dp(10), dp(40)))
        field(minute, 59, onMinute)
        row.addView(text(":", 14f, R.color.dy_faint).apply { gravity = Gravity.CENTER }, LinearLayout.LayoutParams(dp(10), dp(40)))
        field(second, 59, onSecond)
        return row
    }

    private fun syncRuleDuration(rule: StrategyRule): Int {
        val start = rule.hour.coerceIn(0, 23) * 3600 + rule.minute.coerceIn(0, 59) * 60 + rule.second.coerceIn(0, 59)
        val end = rule.endHour.coerceIn(0, 23) * 3600 + rule.endMinute.coerceIn(0, 59) * 60 + rule.endSecond.coerceIn(0, 59)
        var duration = end - start
        if (duration < 0) duration += 86400
        rule.duration = duration.coerceAtMost(65535)
        return rule.duration
    }

    private fun formatDuration(seconds: Int): String = "%d:%02d:%02d · %d 秒".format(seconds / 3600, (seconds / 60) % 60, seconds % 60, seconds)

    private fun addRuleInput(row: LinearLayout, value: Int, minValue: Int, maxValue: Int, onChange: (Int) -> Unit) {
        val input = EditText(this).apply {
            setText(value.toString()); setTextColor(color(R.color.dy_text)); textSize = 12f; gravity = Gravity.CENTER; inputType = InputType.TYPE_CLASS_NUMBER; setSingleLine(); background = rounded(R.color.dy_surface_2, R.color.dy_line, 1, 5); setPadding(dp(3), 0, dp(3), 0)
            setOnFocusChangeListener { _, hasFocus -> if (!hasFocus) { val parsed = text.toString().toIntOrNull()?.coerceIn(minValue, maxValue) ?: minValue; setText(parsed.toString()); onChange(parsed) } }
        }
        row.addView(input, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(40)))
    }

    private fun renderDevices(sensor: Int) {
        if (!::deviceContainer.isInitialized) return
        deviceContainer.removeAllViews()
        ProtocolCodec.devices.chunked(2).forEach { pair ->
            val row = LinearLayout(this).apply { weightSum = 2f }
            pair.forEach { (name, bit) ->
                val on = sensor and bit != 0
                val item = text(
                    if (on) "● $name" else "○ $name",
                    12f,
                    if (on) R.color.dy_teal else R.color.dy_muted
                ).apply {
                    setPadding(dp(10), dp(10), dp(6), dp(10))
                    background = rounded(
                        if (on) R.color.dy_surface_3 else R.color.dy_surface_2,
                        if (on) R.color.dy_teal else R.color.dy_line,
                        1,
                        5
                    )
                }
                row.addView(
                    item,
                    LinearLayout.LayoutParams(0, dp(42), 1f).apply {
                        setMargins(0, 0, dp(5), dp(5))
                    }
                )
            }
            deviceContainer.addView(row)
        }
    }

    private fun applyStar(star: StarStatus) {
        statusTime.text = "%04d-%02d-%02d %02d:%02d:%02d".format(star.year, star.month, star.day, star.hour, star.minute, star.second)
        voltageValue.text = "%.1f V".format(star.voltage)
        ntc1Value.text = "%.1f ℃".format(star.ntc1)
        ntc2Value.text = "%.1f ℃".format(star.ntc2)
        pcbValue.text = "电源板温度 %.1f ℃".format(star.pcb)
        statusSummary.text = "${ProtocolCodec.devices.count { star.sensor and it.second != 0 }} / 8 台设备上电 · 船号 %02d · error %02X".format(star.boat, star.error)
        renderDevices(star.sensor)
        history.addFirst(star); while (history.size > 200) history.removeLast()
        animateUpdate(voltageValue); animateUpdate(ntc1Value); animateUpdate(ntc2Value); animateUpdate(pcbValue)
        linkPulse.pulse()
    }

    private fun sendRead() { val frame = ProtocolCodec.buildFrame(ProtocolCodec.READ, boat()); tx(frame, "\$READ · 读取设备状态") }
    private fun sendGetStrategy() { val frame = ProtocolCodec.buildFrame(ProtocolCodec.GET_STR, boat()); tx(frame, "\$GETSTR · 读取控制策略") }
    private fun sendStrategy() { val frame = ProtocolCodec.buildFrame(ProtocolCodec.STR, boat(), ProtocolCodec.strategyBytes(rules)); tx(frame, "\$STR · 下发 35 条策略") }
    private fun sendRestart() { val frame = ProtocolCodec.buildFrame(ProtocolCodec.REST, boat()); tx(frame, "\$REST · 请求重启板子") }
    private fun sendTime() {
        val parsed = runCatching { LocalDateTime.parse(timeInput.text.toString(), displayDate) }.getOrNull()
        if (parsed == null) { toast("时间格式应为 yyyy-MM-dd HH:mm:ss"); return }
        val epoch = parsed.atZone(ZoneId.systemDefault()).toInstant().toEpochMilli()
        val frame = ProtocolCodec.buildTimeFrame(epoch)
        tx(frame, "${String(frame, Charsets.US_ASCII)} · UTC 校时（无应答）")
        toast("校时已发送，无 ACK 属正常")
    }

    private fun tx(frame: ByteArray, summary: String) {
        if (!connected) { toast("请先连接 OTG 串口"); return }
        addLog("TX", frame, summary)
        usb.send(frame)
        linkPulse.pulse()
    }

    private fun setConnected(value: Boolean, label: String) {
        connected = value
        connectionLabel.text = if (value) "● 已连接 · CH340" else "○ 未连接"
        connectionLabel.contentDescription = label
        connectionLabel.setTextColor(color(if (value) R.color.dy_ok else R.color.dy_muted))
        portLabel = connectionLabel
        gatedButtons.forEach { it.isEnabled = value }
        if (value) linkPulse.start() else linkPulse.stop()
        if (value) toast(label) else stopPolling()
    }

    private fun cyclePolling(button: Button) {
        val values = listOf(0L, 2000L, 5000L, 10000L, 30000L)
        val next = values[(values.indexOf(pollMs) + 1) % values.size]
        pollMs = next
        button.text = if (next == 0L) "关闭" else "每 ${next / 1000} 秒"
        stopPolling()
        if (next > 0 && connected) {
            pollRunnable = object : Runnable { override fun run() { if (connected) { sendRead(); mainHandler.postDelayed(this, pollMs) } } }
            mainHandler.postDelayed(pollRunnable!!, next)
        }
    }

    private fun stopPolling() { pollRunnable?.let { mainHandler.removeCallbacks(it) }; pollRunnable = null; pollMs = 0L }

    private fun confirm(title: String, message: String, action: () -> Unit) {
        AlertDialog.Builder(this).setTitle(title).setMessage(message).setNegativeButton("取消", null).setPositiveButton("继续") { _, _ -> action() }.show()
    }

    private fun addLog(direction: String, bytes: ByteArray, summary: String, error: Boolean = false) {
        if (!::logContainer.isInitialized) return
        val line = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(10), dp(8), dp(10), dp(8)); background = rounded(if (error) R.color.dy_surface_3 else R.color.dy_surface_2, if (error) R.color.dy_danger else R.color.dy_line, 1, 5) }
        val head = text("$direction  ${displayDate.format(LocalDateTime.now())}", 10f, if (error) R.color.dy_danger else R.color.dy_muted)
        val detail = text(if (bytes.isEmpty()) summary else "$summary\n${bytes.toHex()}", 11f, if (error) R.color.dy_danger else R.color.dy_text).apply { typeface = android.graphics.Typeface.MONOSPACE; setPadding(0, dp(4), 0, 0) }
        line.addView(head); line.addView(detail)
        logContainer.addView(line, 0, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 0, 0, dp(6)) })
        while (logContainer.childCount > 120) logContainer.removeViewAt(logContainer.childCount - 1)
        animateIn(line, 0)
    }

    private fun metric(parent: LinearLayout, label: String, value: String, weight: Float): TextView {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(12), dp(10), dp(8), dp(10)); background = rounded(R.color.dy_surface_2, R.color.dy_line, 1, 6) }
        box.addView(text(label, 11f, R.color.dy_muted))
        val valueView = text(value, 20f, R.color.dy_text).apply { setTypeface(typeface, android.graphics.Typeface.BOLD); setPadding(0, dp(6), 0, 0) }
        box.addView(valueView)
        parent.addView(box, LinearLayout.LayoutParams(0, dp(92), weight).apply { setMargins(0, 0, dp(5), dp(5)) })
        return valueView
    }

    private fun panel(kicker: String, title: String, subtitle: String, body: View): View {
        val outer = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = rounded(R.color.dy_surface, R.color.dy_line, 1, 8); setPadding(dp(14), dp(14), dp(14), dp(14)); elevation = dp(3).toFloat() }
        outer.addView(text(kicker, 10f, R.color.dy_faint).apply { letterSpacing = .08f })
        outer.addView(text(title, 18f, R.color.dy_text).apply { setTypeface(typeface, android.graphics.Typeface.BOLD); setPadding(0, dp(3), 0, 0) })
        outer.addView(text(subtitle, 11f, R.color.dy_muted).apply { setPadding(0, dp(2), 0, dp(12)) })
        outer.addView(body)
        val params = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 0, 0, dp(14)) }
        outer.layoutParams = params
        return outer
    }

    private fun button(label: String, backgroundColor: Int, action: (View) -> Unit): Button = Button(this).apply {
        text = label; textSize = 12f; setTextColor(color(if (backgroundColor == R.color.dy_accent) R.color.dy_bg else R.color.dy_text)); isAllCaps = false; minHeight = 0; minimumHeight = 0; setPadding(dp(8), 0, dp(8), 0); background = rounded(backgroundColor, R.color.dy_line, 1, 6); setOnClickListener { view -> action(view) }
    }

    private fun gated(button: Button): Button { button.isEnabled = connected; gatedButtons += button; return button }

    private fun text(value: String, size: Float, colorRes: Int) = TextView(this).apply { text = value; textSize = size; setTextColor(color(colorRes)) }
    private fun rounded(fillRes: Int, strokeRes: Int, strokeWidth: Int, radius: Int) = GradientDrawable().apply { setColor(color(fillRes)); setStroke(dp(strokeWidth), color(strokeRes)); cornerRadius = dp(radius).toFloat() }
    private fun color(res: Int) = getColor(res)
    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()
    private fun toast(message: String) = Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
    private fun boat() = boatInput.text.toString().toIntOrNull()?.coerceIn(1, 255) ?: 1
    private fun animateIn(view: View, delay: Long) { if (!android.animation.ValueAnimator.areAnimatorsEnabled()) { view.alpha = 1f; view.translationY = 0f; return }; view.alpha = 0f; view.translationY = dp(10).toFloat(); view.animate().alpha(1f).translationY(0f).setDuration(320).setStartDelay(delay).setInterpolator(DecelerateInterpolator()).start() }
    private fun animateUpdate(view: View) { if (!android.animation.ValueAnimator.areAnimatorsEnabled()) return; view.animate().alpha(.45f).setDuration(80).withEndAction { view.animate().alpha(1f).setDuration(260).start() }.start() }

    private class LinkPulseView(context: android.content.Context) : View(context) {
        private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        private var running = false
        private var phase = 0f
        private var pulseUntil = 0L
        init { setBackgroundColor(Color.TRANSPARENT) }
        fun start() { running = true; invalidate() }
        fun stop() { running = false; invalidate() }
        fun pulse() { pulseUntil = System.currentTimeMillis() + 500; running = true; invalidate() }
        override fun onDraw(canvas: Canvas) {
            super.onDraw(canvas)
            val cx = width * .12f; val cy = height / 2f; val base = min(width, height) * .14f
            paint.style = Paint.Style.FILL; paint.color = Color.rgb(99, 214, 195); canvas.drawCircle(cx, cy, base, paint)
            paint.style = Paint.Style.STROKE; paint.strokeWidth = dp(1.5f); paint.color = Color.rgb(99, 214, 195)
            val active = running || System.currentTimeMillis() < pulseUntil
            if (active) {
                phase = (phase + .035f) % 1f
                for (index in 0..2) { val p = (phase + index / 3f) % 1f; paint.alpha = ((1f - p) * 150).toInt(); canvas.drawCircle(cx, cy, base + p * base * 6f, paint) }
                postInvalidateDelayed(16)
            }
            paint.alpha = 255; paint.style = Paint.Style.FILL; paint.color = Color.rgb(141, 155, 165); paint.textSize = dp(12f); canvas.drawText(if (running) "USB 链路在线 · 数据会以协议帧流入" else "等待 OTG / CH340 串口授权", dp(30f), cy + dp(4f), paint)
        }
        private fun dp(value: Float) = value * resources.displayMetrics.density
    }
}

private fun ByteArray.toHex(): String = joinToString(" ") { "%02X".format(it.toInt() and 0xFF) }
