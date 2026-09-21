$ErrorActionPreference = 'Stop'

Write-Host 'DY LoRa Console APK build helper'
Write-Host 'Build environment is kept on E:\AndroidBuild (JDK/Gradle/Android SDK).'

$gradle = Get-Command gradle -ErrorAction SilentlyContinue
$javaHome = 'E:\WORK\jre'
$sdkRoot = 'E:\AndroidBuild\android-sdk'
$gradleHome = 'E:\AndroidBuild\.gradle'
$androidUserHome = 'E:\AndroidBuild\.android'
$portableGradle = 'E:\AndroidBuild\gradle\gradle-8.10.2\bin\gradle.bat'
if (Test-Path $javaHome) { $env:JAVA_HOME = $javaHome; $env:PATH = "$javaHome\bin;$env:PATH" }
if (Test-Path $sdkRoot) { $env:ANDROID_SDK_ROOT = $sdkRoot; $env:ANDROID_HOME = $sdkRoot }
New-Item -ItemType Directory -Force $gradleHome, $androidUserHome | Out-Null
$env:GRADLE_USER_HOME = $gradleHome
$env:ANDROID_USER_HOME = $androidUserHome
if (-not $gradle -and (Test-Path $portableGradle)) { $gradle = [pscustomobject]@{ Source = $portableGradle } }
if (-not $gradle) { Write-Error '未找到 Gradle。请安装 Gradle 8.10+，或准备 E:\AndroidBuild 便携构建环境。' }

Push-Location $PSScriptRoot
try {
    & $gradle.Source assembleDebug
    if ($LASTEXITCODE -ne 0) { throw "Gradle 构建失败，退出码 $LASTEXITCODE" }
    Write-Host "APK: $PSScriptRoot\app\build\outputs\apk\debug\app-debug.apk"
}
finally {
    Pop-Location
}
