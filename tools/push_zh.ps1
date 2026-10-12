# Push localized bundles to the device UnityCache
$adb = "E:\AQbot\NoneBo2\fgoapk\platform-tools\adb.exe"
$dev = "4f9b946d"
$src = "E:\AQbot\NoneBo2\fgoapk\work\zh_ta"
$base = "/sdcard/Android/data/com.aniplex.fgowaltz/files/UnityCache/Shared"

& $adb -s $dev shell "am force-stop com.aniplex.fgowaltz"
Start-Sleep -Seconds 2

$files = Get-ChildItem $src -File | Where-Object { $_.Name -like "Shared__*____data" }
foreach ($f in $files) {
    $n = $f.Name
    $core = $n.Substring(8)
    $core = $core.Substring(0, $core.Length - 8)
    $parts = $core -split "__"
    $bundle = $parts[0]
    $hash = $parts[1]
    $dst = "$base/$bundle/$hash/__data"
    $tmp = "/sdcard/_push.bin"
    & $adb -s $dev push $f.FullName $tmp 2>&1 | Out-Null
    & $adb -s $dev shell "su -c 'cp $tmp $dst'" 2>&1 | Out-Null
    & $adb -s $dev shell "su -c 'rm -f $tmp'" | Out-Null
    Write-Host ("  pushed " + $bundle)
}
Write-Host ("done: " + $files.Count + " bundles")
