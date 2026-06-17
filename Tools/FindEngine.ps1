# Locates the Unreal Engine install for KodoTagV2 (EngineAssociation 5.7, with fallbacks).
# Prints the engine root path, or nothing if not found. Diagnostics go to stderr.
$ErrorActionPreference = 'SilentlyContinue'
$cands = New-Object System.Collections.Generic.List[string]

# 1) Registry — how the launcher registers engine installs
foreach ($k in 'HKLM:\SOFTWARE\EpicGames\Unreal Engine', 'HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine') {
    if (Test-Path $k) {
        Get-ChildItem $k | ForEach-Object {
            $dir = (Get-ItemProperty $_.PSPath).InstalledDirectory
            if ($dir) { $cands.Add($dir) }
        }
    }
}

# 2) Epic Launcher manifest
$dat = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat'
if (Test-Path $dat) {
    $json = Get-Content $dat -Raw | ConvertFrom-Json
    $json.InstallationList | Where-Object { $_.AppName -like 'UE_*' } | ForEach-Object { $cands.Add($_.InstallLocation) }
}

# 3) Drive scan of common layouts
foreach ($d in @('C', 'D', 'E', 'F')) {
    foreach ($root in @("${d}:\", "${d}:\Epic Games", "${d}:\Program Files\Epic Games", "${d}:\Games", "${d}:\Games\Epic Games")) {
        Get-ChildItem -Path $root -Filter 'UE_*' -Directory -ErrorAction SilentlyContinue | ForEach-Object { $cands.Add($_.FullName) }
    }
}

$valid = $cands | Where-Object { $_ -and (Test-Path (Join-Path $_ 'Engine\Build\BatchFiles\Build.bat')) } | Select-Object -Unique

[Console]::Error.WriteLine("Candidates checked: $($cands.Count); valid engines: $($valid -join '; ')")

# Prefer the uproject's EngineAssociation (5.7), then newest by name
$best = $valid | Where-Object { $_ -match 'UE_5\.7' } | Select-Object -First 1
if (-not $best) { $best = $valid | Sort-Object { ($_ -replace '.*UE_', '') } -Descending | Select-Object -First 1 }
if ($best) { Write-Output $best }
