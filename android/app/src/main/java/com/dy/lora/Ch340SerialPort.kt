package com.dy.lora

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.util.Log

/**
 * Small, dependency-free CH340/CH341 USB host driver.
 * The app intentionally keeps the driver local so the APK works offline and does not depend on a
 * native library. The protocol layer above it is independent from the USB transport.
 */
class Ch340SerialPort(private val device: UsbDevice) {
    companion object {
        private const val TAG = "DY.Ch340"
        private const val USB_WRITE = 0x40
        private const val USB_READ = 0xC0
        private const val REQ_RESET = 0xA1
        private const val REQ_WRITE_REG = 0x9A
        private const val REQ_MODEM_CTRL = 0xA1
        private const val LCR_ENABLE_RX = 0x80
        private const val LCR_ENABLE_TX = 0x40
        private const val LCR_CS8 = 0x03
        private const val SCL_DTR = 0x20
        private const val SCL_RTS = 0x40

        fun supports(device: UsbDevice): Boolean {
            if (device.vendorId == 0x1A86) return true
            return (0 until device.interfaceCount).any { index ->
                val usbClass = device.getInterface(index).interfaceClass
                usbClass == UsbConstants.USB_CLASS_COMM || usbClass == UsbConstants.USB_CLASS_CDC_DATA
            }
        }
    }

    private var connection: UsbDeviceConnection? = null
    private var usbInterface: UsbInterface? = null
    private var inEndpoint: UsbEndpoint? = null
    private var outEndpoint: UsbEndpoint? = null

    val isOpen: Boolean get() = connection != null && inEndpoint != null && outEndpoint != null

    fun open(newConnection: UsbDeviceConnection): Boolean {
        connection = newConnection
        usbInterface = findInterface(device)
        val iface = usbInterface ?: return false
        if (!newConnection.claimInterface(iface, true)) return false
        for (index in 0 until iface.endpointCount) {
            val endpoint = iface.getEndpoint(index)
            if (endpoint.type != UsbConstants.USB_ENDPOINT_XFER_BULK) continue
            if (endpoint.direction == UsbConstants.USB_DIR_IN) inEndpoint = endpoint else outEndpoint = endpoint
        }
        if (inEndpoint == null || outEndpoint == null) {
            close()
            return false
        }
        if (device.vendorId == 0x1A86) {
            if (!initializeCh340()) {
                Log.w(TAG, "CH340 control initialization returned an error; bulk endpoints remain available")
            }
        }
        return true
    }

    fun close() {
        try { usbInterface?.let { connection?.releaseInterface(it) } } catch (_: Exception) { }
        connection?.close()
        connection = null
        usbInterface = null
        inEndpoint = null
        outEndpoint = null
    }

    fun read(buffer: ByteArray, timeoutMs: Int = 220): Int {
        val conn = connection ?: return -1
        val endpoint = inEndpoint ?: return -1
        return conn.bulkTransfer(endpoint, buffer, buffer.size, timeoutMs)
    }

    fun write(bytes: ByteArray, timeoutMs: Int = 1000): Int {
        val conn = connection ?: return -1
        val endpoint = outEndpoint ?: return -1
        var offset = 0
        while (offset < bytes.size) {
            val written = conn.bulkTransfer(endpoint, bytes, offset, bytes.size - offset, timeoutMs)
            if (written <= 0) return written
            offset += written
        }
        return offset
    }

    private fun findInterface(device: UsbDevice): UsbInterface? {
        for (index in 0 until device.interfaceCount) {
            val candidate = device.getInterface(index)
            val hasBulk = (0 until candidate.endpointCount).count {
                candidate.getEndpoint(it).type == UsbConstants.USB_ENDPOINT_XFER_BULK
            } >= 2
            if (hasBulk) return candidate
        }
        return null
    }

    private fun initializeCh340(): Boolean {
        var ok = true
        ok = controlIn(0x5F, 0, 0, ByteArray(2)) >= 0 && ok
        ok = control(USB_WRITE, REQ_RESET, 0, 0) && ok
        ok = setBaudRate(9600) && ok
        ok = controlIn(0x95, 0x2518, 0, ByteArray(2)) >= 0 && ok
        // 8 data bits, no parity, 1 stop bit; this is the fixed LoRa link format.
        ok = control(USB_WRITE, REQ_WRITE_REG, 0x2518, LCR_ENABLE_RX or LCR_ENABLE_TX or LCR_CS8) && ok
        ok = controlIn(0x95, 0x0706, 0, ByteArray(2)) >= 0 && ok
        // CH34x device mode / line activation sequence used by the Linux and Android drivers.
        ok = control(USB_WRITE, REQ_MODEM_CTRL, 0x501F, 0xD90A) && ok
        ok = setBaudRate(9600) && ok
        // Assert DTR/RTS. Many LoRa carrier boards ignore them, but some CH340 breakouts gate TX.
        ok = control(USB_WRITE, 0xA4, (SCL_DTR or SCL_RTS).inv() and 0xFFFF, 0) && ok
        return ok
    }

    private fun setBaudRate(baudRate: Int): Boolean {
        // CH34x uses a 1,532,620,800 base factor. The two vendor writes below are
        // the same encoding used by usb-serial-for-android and the Linux ch341 driver.
        var factor = 1_532_620_800L / baudRate
        var divisor = 3
        while (factor > 0xFFF0 && divisor > 0) {
            factor = factor shr 3
            divisor--
        }
        if (factor > 0xFFF0) return false
        factor = 0x10000 - factor
        divisor = divisor or 0x0080
        val valueHigh = ((factor and 0xFF00L) or divisor.toLong()).toInt()
        val valueLow = (factor and 0xFFL).toInt()
        return control(USB_WRITE, REQ_WRITE_REG, 0x1312, valueHigh) &&
            control(USB_WRITE, REQ_WRITE_REG, 0x0F2C, valueLow)
    }

    private fun control(requestType: Int, request: Int, value: Int, index: Int): Boolean {
        val result = connection?.controlTransfer(requestType, request, value, index, null, 0, 1000) ?: -1
        return result >= 0
    }

    private fun controlIn(request: Int, value: Int, index: Int, buffer: ByteArray): Int =
        connection?.controlTransfer(USB_READ, request, value, index, buffer, buffer.size, 1000) ?: -1
}
