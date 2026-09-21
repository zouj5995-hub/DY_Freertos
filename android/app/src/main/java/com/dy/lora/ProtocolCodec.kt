package com.dy.lora

import java.io.ByteArrayOutputStream
import java.nio.charset.Charset

data class StarStatus(
    val boat: Int,
    val year: Int,
    val month: Int,
    val day: Int,
    val hour: Int,
    val minute: Int,
    val second: Int,
    val voltage: Double,
    val ntc1: Double,
    val ntc2: Double,
    val pcb: Double,
    val sensor: Int,
    val error: Int
)

data class AckStatus(val type: Int, val boat: Int, val error: Int)

data class StrategyRule(
    var hour: Int = 0,
    var minute: Int = 0,
    var second: Int = 0,
    var duration: Int = 0,
    var target: Int = 0xFF
)

data class ParsedFrame(
    val bytes: ByteArray,
    val type: Type,
    val crcValid: Boolean,
    val legacyLittleEndianCrc: Boolean = false,
    val star: StarStatus? = null,
    val ack: AckStatus? = null,
    val rules: List<StrategyRule>? = null,
    val error: String? = null
) {
    enum class Type { ACK, STAR, STR, UNKNOWN }
}

object ProtocolCodec {
    const val READ = "\$READ"
    const val GET_STR = "\$GETSTR"
    const val STR = "\$STR"
    const val REST = "\$REST"
    const val TAIL = "\$OVER"
    private val charset: Charset = Charsets.US_ASCII

    val devices = listOf(
        "工控机 IPC" to 0x80,
        "声纳 SONAR" to 0x40,
        "备用三 BK3" to 0x20,
        "雷达 RADAR" to 0x10,
        "摄像头 CAMERA" to 0x08,
        "北斗 BD" to 0x04,
        "备用一 BK1" to 0x02,
        "备用二 BK2" to 0x01
    )

    val targets = listOf(
        0x00 to "工控机",
        0x01 to "雷达",
        0x02 to "声纳",
        0x03 to "摄像头",
        0x04 to "北斗",
        0x05 to "备用一",
        0x06 to "备用二",
        0x07 to "备用三",
        0x0A to "工控机 + 雷达 + 声纳 + 摄像头",
        0x0B to "工控机 + 雷达 + 摄像头",
        0x0C to "工控机 + 雷达",
        0x0D to "工控机 + 声纳",
        0xFF to "无效"
    )

    fun crc16Modbus(bytes: ByteArray, length: Int = bytes.size): Int {
        var crc = 0xFFFF
        for (index in 0 until length) {
            crc = crc xor (bytes[index].toInt() and 0xFF)
            repeat(8) {
                crc = if ((crc and 1) != 0) (crc ushr 1) xor 0xA001 else crc ushr 1
            }
        }
        return crc and 0xFFFF
    }

    fun buildFrame(head: String, boat: Int, data: ByteArray = byteArrayOf()): ByteArray {
        val body = ByteArrayOutputStream()
        body.write(head.toByteArray(charset))
        body.write(boat and 0xFF)
        body.write(data)
        val raw = body.toByteArray()
        val crc = crc16Modbus(raw)
        return raw + byteArrayOf((crc shr 8).toByte(), crc.toByte()) + TAIL.toByteArray(charset)
    }

    fun buildTimeFrame(localEpochMillis: Long): ByteArray {
        val local = java.time.Instant.ofEpochMilli(localEpochMillis)
            .atZone(java.time.ZoneId.systemDefault())
            .withZoneSameInstant(java.time.ZoneOffset.UTC)
        val text = String.format(
            java.util.Locale.US,
            "\$BDRMC,%02d-%02d-%02d,%02d:%02d:%02d",
            local.year % 100, local.monthValue, local.dayOfMonth,
            local.hour, local.minute, local.second
        )
        return text.toByteArray(charset)
    }

    fun strategyBytes(rules: List<StrategyRule>): ByteArray {
        val output = ByteArray(35 * 6)
        for (index in 0 until 35) {
            val rule = rules.getOrNull(index) ?: StrategyRule()
            val offset = index * 6
            output[offset] = rule.hour.coerceIn(0, 23).toByte()
            output[offset + 1] = rule.minute.coerceIn(0, 59).toByte()
            output[offset + 2] = rule.second.coerceIn(0, 59).toByte()
            val duration = rule.duration.coerceIn(0, 65535)
            output[offset + 3] = duration.toByte()
            output[offset + 4] = (duration ushr 8).toByte()
            output[offset + 5] = rule.target.toByte()
        }
        return output
    }

    fun parseFrame(bytes: ByteArray): ParsedFrame {
        val type = when {
            bytes.startsWith(ACK_HEAD) -> ParsedFrame.Type.ACK
            bytes.startsWith(STAR_HEAD) -> ParsedFrame.Type.STAR
            bytes.startsWith(STR_HEAD) -> ParsedFrame.Type.STR
            else -> ParsedFrame.Type.UNKNOWN
        }
        if (!bytes.endsWith(TAIL.toByteArray(charset))) {
            return ParsedFrame(bytes, type, false, error = "帧尾不是 \$OVER")
        }
        val received = ((bytes[bytes.size - 7].toInt() and 0xFF) shl 8) or (bytes[bytes.size - 6].toInt() and 0xFF)
        val calculated = crc16Modbus(bytes, bytes.size - 7)
        val legacy = ((bytes[bytes.size - 6].toInt() and 0xFF) shl 8) or (bytes[bytes.size - 7].toInt() and 0xFF)
        val valid = received == calculated || legacy == calculated
        if (!valid) {
            return ParsedFrame(bytes, type, false, error = "CRC 错误：收到 %04X，计算 %04X".format(received, calculated))
        }
        val legacyOrder = received != calculated && legacy == calculated
        return when (type) {
            ParsedFrame.Type.ACK -> ParsedFrame(bytes, type, true, legacyOrder, ack = AckStatus(bytes[4].u8(), bytes[5].u8(), bytes[6].u8()))
            ParsedFrame.Type.STAR -> ParsedFrame(bytes, type, true, legacyOrder, star = parseStar(bytes))
            ParsedFrame.Type.STR -> ParsedFrame(bytes, type, true, legacyOrder, rules = parseRules(bytes))
            else -> ParsedFrame(bytes, type, true, legacyOrder)
        }
    }

    private fun parseStar(bytes: ByteArray): StarStatus = StarStatus(
        boat = bytes[5].u8(),
        year = bytes[6].u8() or (bytes[7].u8() shl 8),
        month = bytes[8].u8(),
        day = bytes[9].u8(),
        hour = bytes[10].u8(),
        minute = bytes[11].u8(),
        second = bytes[12].u8(),
        voltage = (bytes[13].u8() or (bytes[14].u8() shl 8)) / 10.0,
        ntc1 = bytes.s16(15) / 10.0,
        ntc2 = bytes.s16(17) / 10.0,
        pcb = bytes.s16(19) / 10.0,
        sensor = bytes[21].u8(),
        error = bytes[22].u8()
    )

    private fun parseRules(bytes: ByteArray): List<StrategyRule> = buildList {
        repeat(35) { index ->
            val offset = 5 + index * 6
            add(StrategyRule(
                hour = bytes[offset].u8(),
                minute = bytes[offset + 1].u8(),
                second = bytes[offset + 2].u8(),
                duration = bytes[offset + 3].u8() or (bytes[offset + 4].u8() shl 8),
                target = bytes[offset + 5].u8()
            ))
        }
    }

    class StreamParser {
        private var buffer = ByteArray(0)

        fun append(bytes: ByteArray): List<ParsedFrame> {
            buffer += bytes
            val result = mutableListOf<ParsedFrame>()
            while (buffer.isNotEmpty()) {
                val candidates = listOf(
                    ACK_HEAD to 14,
                    STAR_HEAD to 30,
                    STR_HEAD to 222
                )
                val header = candidates.mapNotNull { (head, length) ->
                    val index = buffer.indexOf(head)
                    if (index >= 0) index to Pair(head, length) else null
                }.minByOrNull { it.first } ?: break
                if (header.first > 0) buffer = buffer.copyOfRange(header.first, buffer.size)
                val length = header.second.second
                if (buffer.size < length) break
                val frame = buffer.copyOfRange(0, length)
                buffer = buffer.copyOfRange(length, buffer.size)
                result += parseFrame(frame)
            }
            if (buffer.size > 240) buffer = ByteArray(0)
            return result
        }
    }

    private val ACK_HEAD = "\$ACK".toByteArray(charset)
    private val STAR_HEAD = "\$STAR".toByteArray(charset)
    private val STR_HEAD = "\$STR".toByteArray(charset)

    private fun ByteArray.u8() = toInt() and 0xFF
    private fun ByteArray.s16(offset: Int): Int {
        val value = this[offset].u8() or (this[offset + 1].u8() shl 8)
        return if ((value and 0x8000) != 0) value - 0x10000 else value
    }
    private fun ByteArray.startsWith(prefix: ByteArray): Boolean = size >= prefix.size && prefix.indices.all { this[it] == prefix[it] }
    private fun ByteArray.endsWith(suffix: ByteArray): Boolean = size >= suffix.size && suffix.indices.all { this[size - suffix.size + it] == suffix[it] }
    private fun ByteArray.indexOf(needle: ByteArray): Int {
        if (needle.isEmpty() || size < needle.size) return -1
        for (i in 0..(size - needle.size)) if (needle.indices.all { this[i + it] == needle[it] }) return i
        return -1
    }
}

private operator fun ByteArray.plus(other: ByteArray): ByteArray {
    val result = ByteArray(size + other.size)
    copyInto(result)
    other.copyInto(result, size)
    return result
}
