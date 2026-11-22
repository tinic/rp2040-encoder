# Regenerates the carrier board from gen/design.py: place, autoroute, pour, check, render.
#
#   powershell -File gen\build.ps1 -Freerouting C:\path\to\freerouting-2.5.0.jar
#
# Needs KiCad 10 and Java 21 or later. The routing is redone from scratch on every run.
param(
    [Parameter(Mandatory = $true)][string]$Freerouting,
    [string]$Work = (Join-Path $env:TEMP "rp2040-encoder-carrier")
)
$ErrorActionPreference = "Stop"
$kicad = "C:\Program Files\KiCad\10.0\bin"
$pcbDir = Split-Path -Parent $PSScriptRoot
$board = Join-Path $pcbDir "rp2040-encoder-carrier.kicad_pcb"
New-Item -ItemType Directory -Force $Work | Out-Null
$dsn = Join-Path $Work "carrier.dsn"
$ses = Join-Path $Work "carrier.ses"

function Invoke-KiCadPython([string[]]$Arguments) {
    $old = $env:PATH, $env:PYTHONHOME, $env:PYTHONPATH
    $env:PATH = "$kicad;C:\WINDOWS\system32;C:\WINDOWS"
    $env:PYTHONHOME = $kicad
    $env:PYTHONPATH = "$kicad\Lib\site-packages"
    try { & "$kicad\python.exe" @Arguments; if ($LASTEXITCODE -ne 0) { throw "python failed: $Arguments" } }
    finally { $env:PATH, $env:PYTHONHOME, $env:PYTHONPATH = $old }
}

Invoke-KiCadPython @((Join-Path $PSScriptRoot "gen_pcb.py"), "place", $board, $dsn)

Remove-Item $ses -ErrorAction SilentlyContinue
$env:_JAVA_OPTIONS = "-Xmx2g"
$ErrorActionPreference = "Continue"
& java "-Djava.awt.headless=true" -jar $Freerouting -de $dsn -do $ses -mp 40 "--gui.enabled=false" 2>&1 |
    ForEach-Object { "$_" } | Where-Object { $_ -match "Optimization stage completed|finished with state|unrouted|ERROR" } |
    ForEach-Object { $_.Substring(0, [Math]::Min(200, $_.Length)) }
$ErrorActionPreference = "Stop"
if (-not (Test-Path $ses)) { throw "the autorouter wrote no session file" }

Invoke-KiCadPython @((Join-Path $PSScriptRoot "gen_pcb.py"), "finish", $board, $ses)

& "$kicad\kicad-cli.exe" pcb drc --severity-all --format report --output (Join-Path $Work "drc.rpt") $board | Select-Object -Last 3
Select-String -Path (Join-Path $Work "drc.rpt") -Pattern "^\[" | ForEach-Object { ($_.Line -replace "\(.*", "").Trim() } |
    Group-Object | Sort-Object Count -Descending | ForEach-Object { "{0,4}  {1}" -f $_.Count, $_.Name }
& "$kicad\kicad-cli.exe" pcb render --side top --width 3000 --height 700 --zoom 3.4 -o (Join-Path $Work "top.png") $board | Out-Null
& "$kicad\kicad-cli.exe" pcb render --side bottom --width 3000 --height 700 --zoom 3.4 -o (Join-Path $Work "bottom.png") $board | Out-Null
"renders and DRC report in $Work"
