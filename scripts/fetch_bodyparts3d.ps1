# Fetch BodyParts3D and convert its bone meshes to the .obj this project loads.
#
# BodyParts3D is one segmented human body: every bone is a separate mesh and all
# of them share a single coordinate frame. That is what makes a complete
# skeleton possible — vertebrae, ribs, scapula and skull come already in place,
# with no registration between sources.
#
# Third-party data under CC BY-SA 2.1 Japan (DBCLS). Not redistributed here:
# this fetches it, and the converted meshes stay untracked. See ATTRIBUTION.md.
#
#   powershell -ExecutionPolicy Bypass -File scripts\fetch_bodyparts3d.ps1
param([string]$Out = "data\atlas\bp3d")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dst  = Join-Path $root $Out
$base = 'https://dbarchive.biosciencedbc.jp/data/bodyparts3d/LATEST/'
$zip  = Join-Path $env:TEMP 'bp3d_partof.zip'
$maps = Join-Path $env:TEMP 'bp3d_element_parts.txt'
$tmp  = Join-Path $env:TEMP 'bp3d_extract'

if (-not (Test-Path $zip)) {
    Write-Host "[fetch] BodyParts3D 4.0 meshes (~62 MB)" -ForegroundColor Cyan
    Invoke-WebRequest ($base + 'partof_BP3D_4.0_obj_99.zip') -OutFile $zip -TimeoutSec 1800
} else {
    Write-Host "[fetch] reusing $zip" -ForegroundColor DarkGray
}
if (-not (Test-Path $maps)) {
    Invoke-WebRequest ($base + 'partof_element_parts.txt') -OutFile $maps -TimeoutSec 600
}

if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
Expand-Archive $zip -DestinationPath $tmp -Force

New-Item -ItemType Directory -Force $dst | Out-Null
Write-Host "[convert] naming meshes by anatomy" -ForegroundColor Cyan
python "$root\tools\bp3d_prepare.py" $tmp $maps $dst
if ($LASTEXITCODE -ne 0) { throw "preparation failed" }

$n = (Get-ChildItem $dst -Filter *.obj).Count
$mb = [math]::Round(((Get-ChildItem $dst -Filter *.obj | Measure-Object Length -Sum).Sum / 1MB), 1)
Write-Host "[done] $n bones ($mb MB) in $Out" -ForegroundColor Green
