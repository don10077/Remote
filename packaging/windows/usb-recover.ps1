# Only the Harmony USBLAN device is affected. Other network interfaces are untouched.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
# Elevated commands load only Windows modules, never modules from a user-writable path.
$env:PSModulePath = Join-Path $PSHOME 'Modules'
try {
    $devices = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like 'USB\VID_046D&PID_C11F\*' -and $_.Present -ne $false })
    if ($devices.Count -eq 0) { exit 2 }
    if ($devices.Count -ne 1) { exit 3 }
    $device = $devices[0]
    if ($device.ConfigManagerErrorCode -eq 22) {
        Enable-PnpDevice -InstanceId $device.PNPDeviceID -Confirm:$false
    }
    $adapters = @(Get-CimInstance Win32_NetworkAdapter | Where-Object { $_.PNPDeviceID -eq $device.PNPDeviceID })
    if ($adapters.Count -ne 1 -or -not $adapters[0].NetConnectionID) { exit 4 }
    $adapter = $adapters[0]
    if (-not $adapter.NetEnabled) {
        & (Join-Path ([Environment]::SystemDirectory) 'netsh.exe') interface set interface "name=$($adapter.NetConnectionID)" admin=enabled
        if ($LASTEXITCODE -ne 0) { exit 5 }
    } else {
        # Select by GUID: adapter names can contain wildcard characters.
        $netAdapters = @(Get-NetAdapter -IncludeHidden | Where-Object { $_.InterfaceGuid -eq $adapter.GUID })
        if ($netAdapters.Count -ne 1) { exit 4 }
        Restart-NetAdapter -InputObject $netAdapters[0] -Confirm:$false
    }
    exit 0
} catch { exit 6 }
