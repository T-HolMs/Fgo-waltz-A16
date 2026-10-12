# 重打包 -> zipalign -> 签名 -> 安装(真机 4f9b946d)
param(
  [string]$Apk = "E:\AQbot\NoneBo2\fgoapk\FGO_W_hook.apk",
  [switch]$SkipBuild,
  [switch]$SkipInstall
)
$ErrorActionPreference = "Stop"
$fgo = "E:\AQbot\NoneBo2\fgoapk"
$adb = "$fgo\platform-tools\adb.exe"
$zip = "$fgo\bt\android-14\zipalign.exe"
$sgn = "$fgo\bt\android-14\apksigner.bat"
$kt  = "E:\AQbot\NoneBo2\tools\jre17\jdk-17.0.20.1+1-jre\bin"
$dev = "4f9b946d"

if (-not $SkipBuild) {
  Copy-Item "$fgo\build\libfgowspoof_arm64.so" "$fgo\hooksrc\libfgowspoof_arm64.so" -Force
  python "$fgo\repack_hook.py" "$fgo\src_base.apk" "$fgo\_hooked.apk" | Select-Object -Last 2
}

& $zip -p -f 4 "$fgo\_hooked.apk" "$fgo\_aligned.apk"
if (-not $?) { throw "zipalign failed" }

$env:JAVA_HOME = Split-Path $kt -Parent
$env:PATH = "$kt;$env:PATH"
& $sgn sign --ks "$fgo\fgow_new.jks" --ks-key-alias fgow --ks-pass pass:fgowaltz123 --key-pass pass:fgowaltz123 --v1-signing-enabled true --v2-signing-enabled true --v3-signing-enabled true --out $Apk "$fgo\_aligned.apk"
if (-not $?) { throw "apksigner failed" }

& $sgn verify --print-certs $Apk | Select-Object -First 3
Get-Item $Apk | Select-Object Name, Length

if (-not $SkipInstall) {
  & $adb -s $dev push $Apk /data/local/tmp/install.apk
  & $adb -s $dev shell "su -c 'pm install -r -d /data/local/tmp/install.apk'"
}
