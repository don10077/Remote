$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try {
    $devices = @(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -like 'USB\VID_046D&PID_C11F\*' -and $_.Present -ne $false })
    $result = @{ state = 'absent'; deviceId = ''; adapter = ''; address = ''; detail = '' }
    if ($devices.Count -gt 1) {
        $result.state = 'multiple'
    } elseif ($devices.Count -eq 1) {
        $device = $devices[0]
        $result.deviceId = $device.PNPDeviceID
        $adapter = Get-CimInstance Win32_NetworkAdapter | Where-Object { $_.PNPDeviceID -eq $device.PNPDeviceID }
        if ($device.ConfigManagerErrorCode -eq 22) {
            $result.state = 'disabled'
        } elseif ($device.ConfigManagerErrorCode -eq 28) {
            $result.state = 'driver-missing'
        } elseif ($device.ConfigManagerErrorCode -ne 0) {
            $result.state = 'device-error'
            $result.detail = [string]$device.ConfigManagerErrorCode
        } elseif (-not $adapter) {
            $result.state = 'driver-missing'
        } else {
            $result.adapter = [string]$adapter.NetConnectionID
            if (-not $adapter.NetEnabled) {
                $result.state = 'disabled'
            } else {
                $config = Get-CimInstance Win32_NetworkAdapterConfiguration -Filter "Index = $($adapter.Index)"
                $addresses = @($config.IPAddress | Where-Object { $_ -match '^\d+\.\d+\.\d+\.\d+$' })
                $result.address = $addresses -join ', '
                $result.state = if ($addresses.Count -gt 0) { 'ready' } else { 'no-address' }
            }
        }
    }
    $result | ConvertTo-Json -Compress
} catch {
    @{ state = 'unknown'; detail = $_.Exception.Message } | ConvertTo-Json -Compress
    exit 1
}
