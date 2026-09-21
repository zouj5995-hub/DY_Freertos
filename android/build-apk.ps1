$ErrorActionPreference = 'Stop'

Write-Host 'DY LoRa Console APK build helper'
Write-Host 'This script expects Android Studio/Gradle, Android SDK 35, and JDK 17.'

$gradle = Get-Command gradle -ErrorAction SilentlyContinue
if (-not $gradle) {
    Write-Error '未找到 gradle。请用 Android Studio 打开 android/ 后执行 Build -> Build APK(s)，或安装 Gradle 8.10+。'
}

Push-Location $PSScriptRoot
try {
    & $gradle.Source assembleDebug
    if ($LASTEXITCODE -ne 0) { throw "Gradle 构建失败，退出码 $LASTEXITCODE" }
    Write-Host "APK: $PSScriptRoot\app\build\outputs\apk\debug\app-debug.apk"
}
finally {
    Pop-Location
}
