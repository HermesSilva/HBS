# Launch Arena, the GaitNet visual character editor.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\Arena.ps1                 # empty project
#   powershell -ExecutionPolicy Bypass -File scripts\Arena.ps1 <project.mass>  # open a project
# Inside Arena use File > "Import env.xml..." to load data\env.xml.
param([string]$Project, [ValidateSet("Release","Debug")][string]$Config = "Release")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$rel  = "$root\Dist\x64\$Config"
$vbin = "D:\Tootega\Source\MASS\Deps\vcpkg\installed\x64-windows\bin"

if (-not (Test-Path "$rel\Arena.exe")) { throw "Arena.exe not found; run build.cmd first." }
if ((Test-Path "$vbin\glfw3.dll") -and -not (Test-Path "$rel\glfw3.dll")) { Copy-Item "$vbin\glfw3.dll" $rel }

# Resolve the project against the caller's directory before switching to the
# binary's: a relative path like "data\x.mass" means nothing from Dist\x64\Release.
if ($Project) {
    $resolved = Resolve-Path -LiteralPath $Project -ErrorAction SilentlyContinue
    if ($resolved) { $Project = $resolved.Path }
    elseif (Test-Path -LiteralPath "$root\$Project") { $Project = "$root\$Project" }
    else { throw "project not found: $Project" }
}

$env:KMP_DUPLICATE_LIB_OK = "TRUE"
$env:PATH = "$rel;$env:PATH"
Set-Location $rel
if ($Project) { & "$rel\Arena.exe" $Project } else { & "$rel\Arena.exe" }
