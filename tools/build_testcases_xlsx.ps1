# 由 tools/测试用例数据.txt 生成 docs/功能测试用例.xlsx
# 用法：powershell -ExecutionPolicy Bypass -File tools/build_testcases_xlsx.ps1
$ErrorActionPreference = 'Stop'
$root     = Split-Path -Parent $PSScriptRoot
$dataFile = Join-Path $PSScriptRoot '测试用例数据.txt'
$outFile  = Join-Path $root 'docs\功能测试用例.xlsx'

if (-not (Test-Path $dataFile)) { throw "找不到数据文件：$dataFile" }

# ---------- 1. 读数据 ----------
$lines = @(Get-Content $dataFile -Encoding UTF8 | Where-Object { $_ -match '\S' -and $_ -notmatch '^\s*#' })

$header = @('模块','用例编号','功能项','前置条件','操作步骤','预期结果','示例响应','测试结果','实测现象','测试人','日期')
$table  = New-Object System.Collections.ArrayList
[void]$table.Add($header)
foreach ($ln in $lines) {
    $p = $ln -split '\|'
    if ($p.Count -lt 7) { continue }
    $row = @()
    for ($i = 0; $i -lt 7; $i++) { $row += $p[$i].Trim() }
    $row = @($row[0],$row[1],$row[2],$row[3],$row[4],$row[5],$row[6].Replace('\n',"`n"))   # 示例响应里的 \n 还原为换行
    $row += @('','','','')                                                                 # 结果/现象/测试人/日期 留空待填
    [void]$table.Add($row)
}

# ---------- 2. 其它工作表数据 ----------
$frameHeader = @('用途','帧内容（HEX，串口助手 HEX 模式发送）','长度(字节)','示例响应')
$frameTable = New-Object System.Collections.ArrayList
[void]$frameTable.Add($frameHeader)
$frames = @(
 @('$READ 读设备状态','24 52 45 41 44 01 EB 08 24 4F 56 45 52','13','44 字节 = $ACK(14)+$STAR(30)'),
 @('$GETSTR 读控制策略','24 47 45 54 53 54 52 01 89 E7 24 4F 56 45 52','15','14 字节 $ACK + 222 字节 $STR'),
 @('$REST 重启系统（⚠️会复位）','24 52 45 53 54 01 2E A5 24 4F 56 45 52','13','14 字节 $ACK（成功后约 300ms 复位）'),
 @('船号=02 的 $READ（应被忽略）','24 52 45 41 44 02 EA 48 24 4F 56 45 52','13','无应答'),
 @('坏 CRC 的 $READ（应回失败）','24 52 45 41 44 01 FF FF 24 4F 56 45 52','13','14 字节 $ACK，第 7 字节 error=01'),
 @('未知协议（ASCII 模式发）','$XXXX,test','10','无应答，调试日志：收到未知协议，已丢弃'),
 @('$BDRMC 校时（ASCII 模式发）','$BDRMC,26-09-20,13:02:54','22','无应答；板子本地时间 = UTC + 8')
)
foreach ($f in $frames) { [void]$frameTable.Add($f) }

$fieldHdr = @('帧','字节偏移','长度','含义','示例值')
$fieldTable = New-Object System.Collections.ArrayList
[void]$fieldTable.Add($fieldHdr)
$fields = @(
 @('$ACK','0-3','4','帧头 $ACK','24 41 43 4B'),
 @('$ACK','4','1','类型：0=策略指令 1=控制指令','01'),
 @('$ACK','5','1','船只编号','01'),
 @('$ACK','6','1','错误码：0=成功 1=失败','00'),
 @('$ACK','7-8','2','CRC16（大端）','72 68'),
 @('$ACK','9-13','5','帧尾 $OVER','24 4F 56 45 52'),
 @('$STAR','0-4','5','帧头 $STAR','24 53 54 41 52'),
 @('$STAR','5','1','船只编号','01'),
 @('$STAR','6-7','2','年（小端）','EA 07 = 2026'),
 @('$STAR','8','1','月','09'),
 @('$STAR','9','1','日','15'),
 @('$STAR','10','1','时（本地时间）','05'),
 @('$STAR','11','1','分','02'),
 @('$STAR','12','1','秒','2B'),
 @('$STAR','13-14','2','电池电压（小端，放大 10 倍）','EE 00 = 238 → 23.8V'),
 @('$STAR','15-16','2','NTC1 环境温度（放大 10 倍）','03 01 = 259 → 25.9C'),
 @('$STAR','17-18','2','NTC2 工控机鳍片温度（放大 10 倍）','04 01 = 260 → 26.0C'),
 @('$STAR','19-20','2','PCB 温度（放大 10 倍）','25 01 = 293 → 29.3C'),
 @('$STAR','21','1','sensor_state 设备状态位','04 = 北斗'),
 @('$STAR','22','1','错误码','00'),
 @('$STAR','23-24','2','CRC16（大端）','CD 17'),
 @('$STAR','25-29','5','帧尾 $OVER','24 4F 56 45 52')
)
foreach ($f in $fields) { [void]$fieldTable.Add($f) }

$pinHdr = @('设备','引脚','电压组','sensor_state 位')
$pinTable = New-Object System.Collections.ArrayList
[void]$pinTable.Add($pinHdr)
$pins = @(
 @('工控机 IPC','PE9','24V','0x80'), @('声纳 SONAR','PA0','24V','0x40'),
 @('北斗 BD','PA4','24V','0x04'),    @('备用一 BK1','PC3','24V','0x02'),
 @('雷达 RADAR','PB3','12V','0x10'), @('摄像头 CAMERA','PD7','12V','0x08'),
 @('备用二 BK2','PD3','12V','0x01'), @('备用三 BK3','PA11','12V','0x20'),
 @('12V 总控','PE12','12V','—（不参与逻辑，应始终接通）')
)
foreach ($p in $pins) { [void]$pinTable.Add($p) }

$parHdr = @('参数','当前暂定值','确认来源','备注')
$parTable = New-Object System.Collections.ArrayList
[void]$parTable.Add($parHdr)
$pars = @(
 @('上电电压门槛下限','20.0 V','DC-DC 规格','影响 TC-2.3'),
 @('上电电压门槛上限','31.0 V','DC-DC 规格','影响 TC-2.4'),
 @('工控机过温触发','70 ℃','实机标定','影响 TC-6.2'),
 @('工控机过温恢复','50 ℃','实机标定','影响 TC-6.3'),
 @('PCB 过温触发','85 ℃','实机标定','影响 TC-6.4'),
 @('PCB 过温恢复','70 ℃','实机标定','影响 TC-6.5'),
 @('温度触发确认时间','3 秒','需求确认','影响 TC-6.7'),
 @('温度恢复确认时间','5 秒','需求确认','—'),
 @('工控机安全关机延时','30 秒','工控机开机时长','影响 TC-12.2'),
 @('上报周期（工控机开启）','2 分钟','需求确认','影响 TC-15.2'),
 @('整点上报时刻','每小时第 53 分','需求确认','影响 TC-15.3')
)
foreach ($p in $pars) { [void]$parTable.Add($p) }

# ---------- 3. 写 Excel ----------
function To-Array2D($list) {
    $r = $list.Count; $c = $list[0].Count
    $arr = New-Object 'object[,]' $r, $c
    for ($i = 0; $i -lt $r; $i++) { for ($j = 0; $j -lt $c; $j++) { $arr[$i,$j] = [string]$list[$i][$j] } }
    return ,$arr
}

$xl = New-Object -ComObject Excel.Application
$xl.Visible = $false
$xl.DisplayAlerts = $false
$wb = $xl.Workbooks.Add()

# 工作表数量调整到 5 个
while ($wb.Worksheets.Count -lt 5) { [void]$wb.Worksheets.Add([System.Reflection.Missing]::Value, $wb.Worksheets.Item($wb.Worksheets.Count)) }

$sheets = @(
 @('测试用例',       $table,      @(26,12,22,26,34,34,60,12,26,10,12)),
 @('命令帧速查',     $frameTable, @(30,44,12,46)),
 @('应答帧字段解析', $fieldTable, @(8,10,8,34,26)),
 @('引脚与状态位',   $pinTable,   @(16,10,10,30)),
 @('待确认参数',     $parTable,   @(24,16,18,22))
)

for ($s = 0; $s -lt $sheets.Count; $s++) {
    $name = $sheets[$s][0]; $list = $sheets[$s][1]; $widths = $sheets[$s][2]
    $ws = $wb.Worksheets.Item($s + 1)
    $ws.Name = $name
    $arr = To-Array2D $list
    $rng = $ws.Range('A1').Resize($list.Count, $list[0].Count)
    $rng.Value2 = $arr
    $rng.WrapText = $true
    $rng.VerticalAlignment = -4160     # xlTop
    # 表头样式
    $hdr = $ws.Range('A1').Resize(1, $list[0].Count)
    $hdr.Font.Bold = $true
    $hdr.Interior.Color = 15773696     # 浅蓝
    $hdr.HorizontalAlignment = -4108   # xlCenter
    # 列宽
    for ($j = 0; $j -lt $widths.Count; $j++) { $ws.Columns.Item($j + 1).ColumnWidth = $widths[$j] }
    $ws.Rows.Item(1).RowHeight = 22
    $ws.Application.ActiveWindow.SplitRow = 1
    $ws.Application.ActiveWindow.FreezePanes = $true
}

# 默认停留在第一个工作表
$wb.Worksheets.Item(1).Activate()

if (Test-Path $outFile) { Remove-Item $outFile -Force }
$wb.SaveAs($outFile, 51)   # 51 = xlOpenXMLWorkbook (.xlsx)
$wb.Close($false)
$xl.Quit()
[void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($xl)
[GC]::Collect()

Write-Output ('已生成: ' + $outFile)
Write-Output ('工作表: 测试用例 / 命令帧速查 / 应答帧字段解析 / 引脚与状态位 / 待确认参数')
Write-Output ('用例条数: ' + ($table.Count - 1))
