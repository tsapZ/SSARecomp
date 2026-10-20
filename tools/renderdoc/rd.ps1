<#
.SYNOPSIS
Runs one of the analysis scripts in this folder on a RenderDoc capture, headless, through the
portable RenderDoc in logs\toolchain (see docs/development.md for the download). qrenderdoc owns its
command line, so the capture, the output file and the script arguments go in environment variables.

.USAGE
    tools/renderdoc/rd.ps1 list    <capture.rdc> <out.txt>                    # every action + outputs
    tools/renderdoc/rd.ps1 srvs    <capture.rdc> <out.txt>                    # textures read per draw
    tools/renderdoc/rd.ps1 dump    <capture.rdc> <out.txt> "<dir> <eid>,<eid> [depth]"   # targets as PNG
    tools/renderdoc/rd.ps1 inspect <capture.rdc> <out.txt> "<dir> <eid>,<eid>" # state + input textures
    tools/renderdoc/rd.ps1 timings <capture.rdc> <out.txt>                    # GPU time per pass and action
    tools/renderdoc/rd.ps1 consts  <capture.rdc> <out.txt> "<min> <v>,<v>"    # draws whose PS constants hold these floats
    python3 tools/renderdoc/group.py <list out.txt>                           # list grouped by target
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet("list", "srvs", "dump", "inspect", "timings", "overhead", "consts", "cbuf")]
    [string]$Script,
    [Parameter(Mandatory = $true, Position = 1)] [string]$Capture,
    [Parameter(Mandatory = $true, Position = 2)] [string]$Out,
    [Parameter(Position = 3)] [string]$ScriptArgs = "",
    [int]$TimeoutSec = 600
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$QRenderDoc = Join-Path $RepoRoot "logs\toolchain\RenderDoc_1.46_64\qrenderdoc.exe"
if (-not (Test-Path $QRenderDoc)) { throw "RenderDoc not found at $QRenderDoc (see docs/development.md)" }

$env:RD_DIR = $PSScriptRoot
$env:RD_CAP = (Resolve-Path $Capture).Path
$env:RD_OUT = $Out
$env:RD_ARGS = $ScriptArgs
if (Test-Path $Out) { Remove-Item $Out }
$p = Start-Process -FilePath $QRenderDoc -ArgumentList "--python `"$PSScriptRoot\$Script.py`"" `
    -WindowStyle Minimized -PassThru
if (-not $p.WaitForExit($TimeoutSec * 1000)) { Stop-Process -Id $p.Id -Force; throw "timed out" }
if (Test-Path $Out) { "wrote $Out" } else { throw "the script wrote no output (did it fail to import?)" }
