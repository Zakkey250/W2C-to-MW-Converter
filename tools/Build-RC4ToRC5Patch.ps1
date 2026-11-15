param(
    [Parameter(Mandatory=$true)][string]$Python,
    [Parameter(Mandatory=$true)][string]$RuntimeAsi
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$source = Join-Path $root 'public-converter'
$patcher = Join-Path $source 'patch_rc4_to_rc5.py'
$runtime = (Resolve-Path -LiteralPath $RuntimeAsi).Path
$expected = [regex]::Match((Get-Content -LiteralPath $patcher -Raw), '(?m)^UPDATE = "([a-f0-9]{64})"\r?$').Groups[1].Value
if (!$expected -or (Get-FileHash -LiteralPath $runtime).Hash -ine $expected) {
    throw 'Supply the verified RC5 W2CMWPublic.asi from the published package.'
}

$build = Join-Path $root 'build/rc4-to-rc5-patch'
$dist = Join-Path $build 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
& $Python -m PyInstaller --noconfirm --onefile --console --noupx --name 'W2CMW-RC4-to-RC5-Patch' `
    --distpath $dist --workpath (Join-Path $build 'work') --specpath $build `
    --exclude-module build_world --exclude-module pursuit `
    --add-data "$(Join-Path $source 'catalog.json');." --add-data "$(Join-Path $source 'rules.json');." $patcher
if ($LASTEXITCODE) { throw 'Unable to build the RC4-to-RC5 patcher.' }
Copy-Item -LiteralPath $runtime -Destination (Join-Path $dist 'W2CMWPublic.asi')
