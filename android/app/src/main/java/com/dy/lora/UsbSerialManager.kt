package com.dy.lora

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.os.Build
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

class UsbSerialManager(
    private val context: Context,
    private val listener: Listener
) {
    interface Listener {
        fun onConnectionState(connected: Boolean, label: String)
        fun onBytesReceived(bytes: ByteArray)
        fun onTransportLog(message: String, error: Boolean = false)
    }

    companion object {
        private const val ACTION_USB_PERMISSION = "com.dy.lora.USB_PERMISSION"
    }

    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private val ioExecutor: ExecutorService = Executors.newSingleThreadExecutor()
    private val reading = AtomicBoolean(false)
    private var serial: Ch340SerialPort? = null
    private var openedDevice: UsbDevice? = null
    private var connection: UsbDeviceConnection? = null

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == ACTION_USB_PERMISSION) {
                val device = parcelableDevice(intent)
                if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false) && device != null) openDevice(device) else listener.onTransportLog("用户未授予 USB 权限", true)
            }
            if (intent.action == UsbManager.ACTION_USB_DEVICE_DETACHED) {
                val detached = parcelableDevice(intent)
                if (detached != null && detached == openedDevice) close()
            }
        }
    }

    fun register() {
        val filter = IntentFilter().apply {
            addAction(ACTION_USB_PERMISSION)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
        }
        if (Build.VERSION.SDK_INT >= 33) context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED) else registerLegacyReceiver(filter)
    }

    fun unregister() {
        try { context.unregisterReceiver(receiver) } catch (_: Exception) { }
        close()
        ioExecutor.shutdownNow()
    }

    fun requestFirstSupportedDevice() {
        val device = usbManager.deviceList.values.firstOrNull { Ch340SerialPort.supports(it) }
        if (device == null) {
            listener.onTransportLog("未发现 CH340/USB 串口，请检查 OTG、供电和线材", true)
            return
        }
        if (usbManager.hasPermission(device)) openDevice(device) else {
            val intent = Intent(ACTION_USB_PERMISSION).setPackage(context.packageName)
            val flags = PendingIntent.FLAG_UPDATE_CURRENT or if (Build.VERSION.SDK_INT >= 23) PendingIntent.FLAG_IMMUTABLE else 0
            usbManager.requestPermission(device, PendingIntent.getBroadcast(context, 0, intent, flags))
            listener.onTransportLog("等待 USB 权限确认…")
        }
    }

    fun send(bytes: ByteArray): Boolean {
        val transport = serial ?: return false
        ioExecutor.execute {
            val count = transport.write(bytes)
            if (count == bytes.size) listener.onTransportLog("发送 ${bytes.size} 字节") else listener.onTransportLog("串口发送失败：$count / ${bytes.size}", true)
        }
        return true
    }

    fun close() {
        reading.set(false)
        serial?.close()
        serial = null
        connection = null
        openedDevice = null
        listener.onConnectionState(false, "未连接")
    }

    private fun openDevice(device: UsbDevice) {
        close()
        val conn = usbManager.openDevice(device)
        if (conn == null) {
            listener.onTransportLog("无法打开 USB 设备", true)
            return
        }
        val candidate = Ch340SerialPort(device)
        if (!candidate.open(conn)) {
            conn.close()
            listener.onTransportLog("CH340 接口初始化失败", true)
            return
        }
        connection = conn
        serial = candidate
        openedDevice = device
        val label = "CH340 · ${device.deviceName}"
        listener.onConnectionState(true, label)
        listener.onTransportLog("USB 串口已打开 · 9600 / 8N1")
        startReader(candidate)
    }

    private fun startReader(transport: Ch340SerialPort) {
        if (!reading.compareAndSet(false, true)) return
        ioExecutor.execute {
            val buffer = ByteArray(512)
            while (reading.get() && transport.isOpen) {
                val count = transport.read(buffer)
                if (count > 0) listener.onBytesReceived(buffer.copyOf(count))
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun registerLegacyReceiver(filter: IntentFilter) {
        context.registerReceiver(receiver, filter)
    }

    @Suppress("DEPRECATION")
    private fun parcelableDevice(intent: Intent): UsbDevice? {
        return if (Build.VERSION.SDK_INT >= 33) intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java) else intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
    }
}
