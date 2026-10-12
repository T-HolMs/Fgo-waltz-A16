# Restore all UnityCache bundles from the pristine extraction
$adb = "E:\AQbot\NoneBo2\fgoapk\platform-tools\adb.exe"
$dev = "4f9b946d"
$src = "E:\AQbot\NoneBo2\fgoapk\work\uc"
$base = "/sdcard/Android/data/com.aniplex.fgowaltz/files/UnityCache/Shared"

& $adb -s $dev shell "am force-stop com.aniplex.fgowaltz"
Start-Sleep -Seconds 2

$files = Get-ChildItem $src -File | Where-Object { $_.Name -like "Shared__*____data" }
$n = 0
foreach ($f in $files) {
    $nm = $f.Name
    $core = $nm.Substring(8)
    $core = $core.Substring(0, $core.Length - 8)
    $parts = $core -split "__"
    $dst = "$base/" + $parts[0] + "/" + $parts[1] + "/__data"
    $tmp = "/sdcard/_rb.bin"
    & $adb -s $dev push $f.FullName $tmp 2>&1 | Out-Null
    & $adb -s $dev shell "su -c 'cp $tmp $dst'" 2>&1 | Out-Null
    $n++
}
& $adb -s $dev shell "su -c 'rm -f /sdcard/_rb.bin'"
Write-Host ("restored " + $n + " bundles")
