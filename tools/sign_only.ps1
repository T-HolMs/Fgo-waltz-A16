# 对已有 APK 做 zipalign + 签名 + 安装
param(
  [Parameter(Mandatory=$true)][string]$In,
  [Parameter(Mandatory=$true)][string]$Out,
  [switch]$SkipInstall
)
$ErrorActionPreference = "Stop"
$fgo = "E:\AQbot\NoneBo2\fgoapk"
$adb = "$fgo\platform-tools\adb.exe"
$zip = "$fgo\bt\android-14\zipalign.exe"
$sgn = "$fgo\bt\android-14\apksigner.bat"
$kt  = "E:\AQbot\NoneBo2\tools\jre17\jdk-17.0.20.1+1-jre\bin"

if (-not (Test-Path $In)) { throw "input not found: $In" }
if (-not (Test-Path "$fgo\_sign_tmp")) { New-Item -ItemType Directory -Path "$fgo\_sign_tmp" | Out-Null }
$aligned = "$fgo\_sign_tmp\aligned.apk"
Remove-Item $aligned -Force -ErrorAction SilentlyContinue

& $zip -p -f 4 $In $aligned
if (-not $?) { throw "zipalign failed" }

$env:JAVA_HOME = Split-Path $kt -Parent
$env:PATH = "$kt;$env:PATH"
& $sgn sign --ks "$fgo\fgow_new.jks" --ks-key-alias fgow --ks-pass pass:fgowaltz123 `
    --key-pass pass:fgowaltz123 --v1-signing-enabled true --v2-signing-enabled true `
    --v3-signing-enabled true --out $Out $aligned
if (-not $?) { throw "apksigner failed" }
Get-Item $Out | Select-Object Name, Length

if (-not $SkipInstall) {
  $adbDev = "4f9b946d"
  & $adb -s $adbDev push $Out /data/local/tmp/install.apk
  & $adb -s $adbDev shell "su -c 'pm install -r -d /data/local/tmp/install.apk'"
}
