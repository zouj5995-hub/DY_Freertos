# 给 App 与 Core 下的源文件补 UTF-8 BOM
# 原因：CubeMX 每次重新生成代码都会写成"无 BOM 的 UTF-8"，
#       而 ARMCC 在无 BOM 时按 GBK 解析中文，会导致 missing closing quote 等编译错误。
# 用法：在工程根目录执行  powershell -ExecutionPolicy Bypass -File tools\fix_utf8_bom.ps1
$root = Split-Path -Parent $PSScriptRoot
$encStrict = New-Object System.Text.UTF8Encoding($false, $true)
$encBom    = New-Object System.Text.UTF8Encoding($true)
$fixed = @()
Get-ChildItem "$root\App", "$root\Core" -Recurse -Include *.c, *.h -File | ForEach-Object {
    $b = [System.IO.File]::ReadAllBytes($_.FullName)
    if (-not ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)) {
        try {
            [System.IO.File]::WriteAllText($_.FullName, $encStrict.GetString($b), $encBom)
            $fixed += $_.FullName.Replace($root + '\', '')
        } catch {
            Write-Output ("跳过（不是合法 UTF-8）: " + $_.Name)
        }
    }
}
Write-Output ("已补 BOM: " + $fixed.Count + " 个文件")
$fixed | ForEach-Object { "  $_" }