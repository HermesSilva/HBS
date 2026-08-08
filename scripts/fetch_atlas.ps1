# Fetch the OpenSim reference models used as HBS's muscle atlas.
#
# These are third-party research models. They are not redistributed here: this
# script downloads them, the same way scripts\fetch_bones.ps1 fetches the bone
# meshes. See ATTRIBUTION.md for sources and terms.
#
#   powershell -ExecutionPolicy Bypass -File scripts\fetch_atlas.ps1
param([string]$Out = "data\atlas")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dst  = Join-Path $root $Out
New-Item -ItemType Directory -Force $dst | Out-Null

$base = 'https://raw.githubusercontent.com/opensim-org/opensim-models/master/'
$models = @{
    'Models/Gait2392_Simbody/gait2392_thelen2003muscle.osim' = 'lower limb + trunk muscles'
    'Models/Arm26/arm26.osim'                                = 'upper arm muscles'
    'Models/WristModel/wrist.osim'                           = 'forearm, wrist and hand'
    'Models/Rajagopal/RajagopalLaiUhlrich2023.osim'          = 'full-body skeleton'
    'Models/Hamner/FullBodyModel_Hamner2010_v2_0.osim'       = 'full-body, running'
}

foreach ($path in $models.Keys) {
    $name = Split-Path $path -Leaf
    $target = Join-Path $dst $name
    if (Test-Path $target) { Write-Host "[have] $name" -ForegroundColor DarkGray; continue }
    try {
        Invoke-WebRequest ($base + $path) -OutFile $target -TimeoutSec 300
        Write-Host ("[get ] {0,-42} {1}" -f $name, $models[$path]) -ForegroundColor Cyan
    } catch {
        Write-Host "[fail] $name : $($_.Exception.Message)" -ForegroundColor Red
    }
}

$n = (Get-ChildItem $dst -Filter *.osim).Count
Write-Host "[done] $n reference models in $Out" -ForegroundColor Green
