#requires -Version 5.1
<#
.SYNOPSIS
  Junkyard Windows build: configure, build (Release, x64), test, pluginval, package the VST3 zip.
.PARAMETER Install     Copy the bundle to C:\Program Files\Common Files\VST3\ (needs an elevated shell).
.PARAMETER NoTests     Skip ctest.
.PARAMETER NoValidate  Skip pluginval.
.PARAMETER NoPackage   Skip the dist\ zip.
#>
[CmdletBinding()]
param([switch]$Install, [switch]$NoTests, [switch]$NoValidate, [switch]$NoPackage)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Fail($msg) { Write-Host "!! $msg" -ForegroundColor Red; exit 1 }
function Step($msg) { Write-Host "== $msg" -ForegroundColor Cyan }

# ---- 1. tools -------------------------------------------------------------
Step 'tools'
$cm = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cm) { Fail 'cmake not found. Install: winget install --id Kitware.CMake -e   (then open a new terminal)' }
$cmVer = [version]([regex]::Match((& cmake --version | Select-Object -First 1), '\d+\.\d+\.\d+').Value)
if ($cmVer -lt [version]'3.24') { Fail "cmake $cmVer is too old, need 3.24 or newer" }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { Fail 'Visual Studio 2022 Build Tools not found. Install: winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --wait"' }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { Fail 'Visual Studio is installed but the "Desktop development with C++" (VC tools x86/x64) component is missing. Re-run the Build Tools installer and add it.' }
Write-Host "cmake $cmVer, Visual Studio at $vsPath"

$Ver = ((Get-Content (Join-Path $Root 'cmake\VersionNumber.cmake') -Raw) -replace '(?s).*set\(JUNKYARD_VERSION\s+([0-9A-Za-z\.\-]+)\).*', '$1').Trim()
Write-Host "Junkyard $Ver"

# ---- 2/3. configure + build -----------------------------------------------
Step 'configure'
$extra = @()
if ($env:JUNKYARD_DEPS_DIR) { $extra += "-DFETCHCONTENT_BASE_DIR=$($env:JUNKYARD_DEPS_DIR)" }   # CI cache hook
& cmake -S . -B build-win -G 'Visual Studio 17 2022' -A x64 -DJUNKYARD_DEV_BUILD=OFF @extra
if ($LASTEXITCODE -ne 0) { Fail 'cmake configure failed' }
Step 'build (Release)'
& cmake --build build-win --config Release --parallel
if ($LASTEXITCODE -ne 0) { Fail 'build failed' }

$Vst3 = Join-Path $Root 'build-win\Junkyard_artefacts\Release\VST3\Junkyard.vst3'
if (-not (Test-Path $Vst3)) { Fail "VST3 bundle not found at $Vst3" }
$Dll = Join-Path $Vst3 'Contents\x86_64-win\Junkyard.vst3'
if (-not (Test-Path $Dll)) { Fail "plug-in DLL not found at $Dll" }

# ---- 4. tests -------------------------------------------------------------
if (-not $NoTests) {
  Step 'tests'
  & ctest --test-dir build-win -C Release --output-on-failure
  if ($LASTEXITCODE -ne 0) { Fail 'tests failed (sort failures per the tolerance policy in docs/DECISIONS.md: bit-exact determinism failures are bugs, report them)' }
}

# ---- 5. pluginval ---------------------------------------------------------
if (-not $NoValidate) {
  Step 'pluginval strictness 10'
  $pvDir = Join-Path $Root 'tools\bin\win'
  $pv = Join-Path $pvDir 'pluginval.exe'
  if (-not (Test-Path $pv)) {
    New-Item -ItemType Directory -Force $pvDir | Out-Null
    $zip = Join-Path $pvDir 'pluginval_Windows.zip'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip' -OutFile $zip
    Expand-Archive -Force $zip $pvDir
    Remove-Item $zip
  }
  $log = Join-Path $Root 'build-win\pluginval-vst3.log'
  $prevEap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'   # Windows PowerShell 5.1 turns native stderr into errors when it is redirected
  & $pv --strictness-level 10 --validate-in-process --skip-gui-tests --timeout-ms 300000 $Vst3 2>&1 | Tee-Object -FilePath $log | Out-Null
  $pvExit = $LASTEXITCODE
  $ErrorActionPreference = $prevEap
  Get-Content $log -Tail 3
  if ($pvExit -ne 0) { Get-Content $log -Tail 40; Fail 'pluginval failed' }
}

# ---- 6. static runtime check ----------------------------------------------
Step 'static runtime check'
$dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
if (-not $dumpbin) {
  # try the VS tool directory
  $cand = Get-ChildItem (Join-Path $vsPath 'VC\Tools\MSVC') -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match 'Hostx64\\x64' } | Select-Object -First 1
  if ($cand) { $dumpbin = $cand.FullName }
}
if ($dumpbin) {
  $deps = & $dumpbin /dependents $Dll | Out-String
  if ($deps -match '(?i)VCRUNTIME140|MSVCP140') { Fail "the DLL depends on the dynamic VC runtime:`n$deps" }
  Write-Host 'OK: no VCRUNTIME140.dll / MSVCP140.dll dependency'
} else { Write-Host '(dumpbin not found, static runtime check skipped; run from a "Developer PowerShell for VS" to enable it)' }

# ---- 7. package -----------------------------------------------------------
if (-not $NoPackage) {
  Step 'package'
  $dist = Join-Path $Root 'dist'
  $name = "Junkyard-$Ver-windows-x64"
  $stage = Join-Path $dist $name
  if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
  New-Item -ItemType Directory -Force $stage | Out-Null
  Copy-Item -Recurse $Vst3 (Join-Path $stage 'Junkyard.vst3')
  Copy-Item (Join-Path $Root 'packaging\INSTALL-windows.txt') (Join-Path $stage 'INSTALL.txt')
  Copy-Item (Join-Path $Root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
  Copy-Item (Join-Path $Root 'THIRD_PARTY_NOTICES.md') (Join-Path $stage 'THIRD_PARTY_NOTICES.txt')
  $repo = ((Get-Content (Join-Path $Root 'cmake\Version.cmake') -Raw) -replace '(?s).*set\(JUNKYARD_REPO_URL\s+"([^"]+)"\).*', '$1').Trim()
  @("Junkyard $Ver is free software under the GNU AGPL-3.0.",
    'The complete corresponding source code of this release is at:',
    "  $repo",
    "  tag: v$Ver") | Set-Content -Encoding ASCII (Join-Path $stage 'SOURCE.txt')
  $zipPath = Join-Path $dist "$name.zip"
  if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
  Compress-Archive -Path $stage -DestinationPath $zipPath
  Remove-Item -Recurse -Force $stage
  $hash = (Get-FileHash -Algorithm SHA256 $zipPath).Hash.ToLower()
  "$hash  $name.zip" | Set-Content -Encoding ASCII (Join-Path $dist 'SHA256SUMS-windows.txt')
  Write-Host "Packaged $zipPath"
  Write-Host "SHA256 $hash"
}

# ---- 8. install -----------------------------------------------------------
if ($Install) {
  Step 'install'
  $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  if (-not $admin) { Fail '-Install needs an elevated (Administrator) PowerShell' }
  $dst = 'C:\Program Files\Common Files\VST3\Junkyard.vst3'
  if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
  Copy-Item -Recurse -Force $Vst3 $dst
  Write-Host "Installed to $dst. Restart your DAW and rescan."
}
Step "done: Junkyard $Ver"
