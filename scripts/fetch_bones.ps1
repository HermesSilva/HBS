# Fetch the OpenSim bone meshes and convert them to .obj under data/atlas/bones.
#
# These are one mesh per bone — every carpal, every vertebra, the jaw — in the
# same coordinate frame as the .osim models already used as the muscle atlas, so
# bones and muscle paths come from one consistent source.
#
# The .obj are derived data and are not tracked: run this to (re)create them.
#
#   powershell -ExecutionPolicy Bypass -File scripts\fetch_bones.ps1
param(
    [string]$Out = "data\atlas\bones",
    [switch]$KeepArchive
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dst  = Join-Path $root $Out
$zip  = Join-Path $env:TEMP "opensim-models.zip"
$tmp  = Join-Path $env:TEMP "opensim-models"

if (-not (Test-Path $zip)) {
    Write-Host "[fetch] opensim-models (~114 MB)" -ForegroundColor Cyan
    Invoke-WebRequest 'https://codeload.github.com/opensim-org/opensim-models/zip/refs/heads/master' `
        -OutFile $zip -TimeoutSec 900
} else {
    Write-Host "[fetch] reusing $zip" -ForegroundColor DarkGray
}

if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
Expand-Archive $zip -DestinationPath $tmp -Force
$geo = Join-Path $tmp "opensim-models-master\Geometry"
if (-not (Test-Path $geo)) { throw "Geometry/ not found in the archive" }

Write-Host "[convert] vtp -> obj" -ForegroundColor Cyan
python "$root\tools\vtp2obj.py" $geo $dst
if ($LASTEXITCODE -gt 2) { throw "conversion failed" }

# Scene props and primitives that ship alongside the bones, plus the fused
# whole-hand/whole-foot meshes this project replaces with individual bones.
$drop = @('ConePendulum','Cube','anchor1','anchor2','arrow','axes','big_block_centered','block',
          'blockMesh','box','bucket','checkered_floor','com','cone','cylinder','ellipsoid',
          'ellipsoid_center','line','linkage1','outline','pendulum','plane','sphere','treadmill',
          'unit_plane','ground_jaw','ground_r_clavicle','ground_r_scapula','ground_ribs',
          'ground_skull','ground_spine','foot') + (1..19 | ForEach-Object { "fingers$_" })
$removed = 0
foreach ($n in $drop) {
    $p = Join-Path $dst "$n.obj"
    if (Test-Path $p) { Remove-Item $p; $removed++ }
}

if (-not $KeepArchive) { Remove-Item $tmp -Recurse -Force }
$kept = (Get-ChildItem $dst -Filter *.obj).Count
$mb   = [math]::Round(((Get-ChildItem $dst -Filter *.obj | Measure-Object Length -Sum).Sum / 1MB), 1)
Write-Host "[done] $kept bones ($mb MB) in $Out; dropped $removed non-bones" -ForegroundColor Green
