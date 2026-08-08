# Rebuild data/Modelo02.mass from data/Modelo01.mass.
#
# Modelo01 is the 304-muscle model the shipped networks were trained against and
# is never modified. Modelo02 is the anatomically completed one: every edit lives
# here, so it can be replayed from scratch after any change to the tools or to
# data/atlas/missing_muscles.json.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build_model02.ps1
param(
    [int]$Port = 8766,
    [string]$Source = "data\Modelo01.mass",
    [string]$Target = "data/Modelo02.mass"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe  = "$root\Dist\x64\Release\gaitnet-mcp.exe"
if (-not (Test-Path $exe)) { throw "build the MCP first: cmake --build build --config Release" }

Get-Process gaitnet-mcp -ErrorAction SilentlyContinue | Stop-Process -Force
$srv = Start-Process -FilePath $exe -ArgumentList "$root\$Source", $Port -PassThru `
                     -WindowStyle Hidden -RedirectStandardOutput "$env:TEMP\build_model02.log"
Start-Sleep -Milliseconds 900

# The atlas is stacked from three OpenSim models, none of which covers the whole
# body, plus the synonym table that joins their abbreviated names to ours.
$calls = @(
    'load_atlas {"path":"data/atlas/gait2392_thelen2003muscle.osim"}'
    'load_atlas {"path":"data/atlas/arm26.osim"}'
    'load_atlas {"path":"data/atlas/wrist.osim"}'
    'load_atlas {"synonyms":"data/atlas/synonyms.json"}'

    # --- corrections to muscles inherited from Modelo01 ---
    # the infraspinatus sat entirely on the scapula, crossing no joint; it
    # inserts on the greater tubercle of the humerus
    'reanchor_waypoints {"muscle":"L_Infraspinatus1","from":"ShoulderL","to":"ArmL","firstIndex":2}'
    # longissimus thoracis and iliocostalis end at thoracic ribs, not the neck
    'reanchor_waypoints {"muscle":"L_Longissimus_Thoracis","from":"Neck","to":"Torso"}'
    'reanchor_waypoints {"muscle":"L_iliocostalis","from":"Neck","to":"Torso"}'
    # lmax differed between sides (the field is dead in the engine, but the data
    # should still match)
    'set_muscle {"name":"L_Tibialis_Anterior","hill":{"lmax":-0.1}}'

    # --- missing bones first: the digital tendons insert on the phalanges, so
    # --- they have to exist before the muscles referencing them are added
    'generate_fingers {"hand":"HandL","symmetric":true}'
    'add_muscles {"path":"data/atlas/missing_muscles.json"}'

    # digital tendons now run to the phalanges instead of stopping at the palm
    'reanchor_waypoints {"muscle":"L_Extensor_Digitorum1","from":"HandL","to":"HandMiddle3L","lastOnly":true,"snap":true}'
    'reanchor_waypoints {"muscle":"L_Flexor_Digitorum_Profundus2","from":"HandL","to":"HandMiddle3L","lastOnly":true,"snap":true}'
    'reanchor_waypoints {"muscle":"L_Flexor_Digitorum_Superficialis","from":"HandL","to":"HandMiddle2L","lastOnly":true,"snap":true}'
    'reanchor_waypoints {"muscle":"L_Extensor_Digiti_Minimi","from":"HandL","to":"HandLittle3L","lastOnly":true,"snap":true}'
    'reanchor_waypoints {"muscle":"L_Extensor_Pollicis_Longus","from":"HandL","to":"HandThumb2L","lastOnly":true,"snap":true}'
    'reanchor_waypoints {"muscle":"L_Flexor_Pollicis_Longus","from":"HandL","to":"HandThumb2L","lastOnly":true,"snap":true}'
    'reanchor_waypoints {"muscle":"L_Extensor_Pollicis_Brevis","from":"HandL","to":"HandThumb1L","lastOnly":true,"snap":true}'

    # --- Hill parameters and PCSA from the atlas, then write it out ---
    'sync_from_atlas {"fillHill":true}'
    "save {`"path`":`"$Target`"}"
)

& "$PSScriptRoot\mcp_call.ps1" -Call $calls -Port $Port |
    Select-String -Pattern 'error|"created"|"changed"|"patched"|"ok"'

Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue
Write-Host "[done] $Target" -ForegroundColor Green
