param([string]$Python, [string]$VaultToolReference, [string]$ReuseVaultHelper, [string]$BuildRoot)

$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot
$desktop=Join-Path $project 'public-converter'
$build=if($BuildRoot){[System.IO.Path]::GetFullPath($BuildRoot)}else{Join-Path $project 'build/desktop'}
New-Item -ItemType Directory -Path $build -Force | Out-Null
if(!$Python){
    $Python=Join-Path $build 'clean-env/Scripts/python.exe'
    if(!(Test-Path -LiteralPath $Python -PathType Leaf)){
        py -3.12 -m venv (Join-Path $build 'clean-env')
        if($LASTEXITCODE){throw 'Unable to create the converter build environment'}
    }
}
& $python -m pip install --disable-pip-version-check 'pyinstaller==6.22.3' 'numpy==2.4.3' 'scipy==1.18.1' 'Pillow==12.1.1' 'PyYAML==6.0.3' 'lz4==4.4.5' 'pefile==2024.8.26' 'unicorn==2.1.4'
if($LASTEXITCODE){throw 'Unable to install the converter build dependencies'}

if($ReuseVaultHelper){
    $vault=(Resolve-Path -LiteralPath $ReuseVaultHelper).Path
}else{
    if(!$VaultToolReference){throw 'Pass -VaultToolReference with the path to the OGVI/Attribulator tool folder'}
    $VaultToolReference=(Resolve-Path -LiteralPath $VaultToolReference).Path
    $vaultDir=Join-Path $build 'vault-compiler'
    & dotnet publish (Join-Path $project 'compiler/public-vault/PublicVault.csproj') -c Release -o $vaultDir /p:PublishSingleFile=true /p:DebugType=None /p:DebugSymbols=false /nodeReuse:false "/p:VaultToolReference=$VaultToolReference"
    if($LASTEXITCODE){throw 'Unable to build the public vault helper'}
    $vault=Join-Path $vaultDir 'PublicVault.exe'
}
$repacker=Join-Path $project 'compiler/native/bin/W2CMWv2Repack.exe'
$rules=Join-Path $desktop 'rules.json'
$rollbackDir=Join-Path $build 'rollback'
$rollback=Join-Path $rollbackDir 'W2CMW-Rollback.exe'
$rollbackArgs=@(
    '--noconfirm','--noupx','--onefile','--windowed','--name','W2CMW-Rollback',
    '--distpath',$rollbackDir,'--workpath',(Join-Path $build 'pyinstaller-rollback'),'--specpath',$build,
    '--exclude-module','build_world','--exclude-module','pursuit',
    '--add-data',"$rules;.",'--add-data',"$desktop/catalog.json;.",
    '--add-data',"$desktop/translations.json;.",
    '--add-data',"$desktop/components-catalog.json;."
)
& $python -m PyInstaller @rollbackArgs (Join-Path $desktop 'rollback_app.py')
if($LASTEXITCODE){throw 'Unable to build the standalone rollback executable'}
$identity=Join-Path $build 'compiler-identity.json'
$rows=@{}
foreach($path in @($repacker,$vault,$rules,(Join-Path $desktop 'catalog.json'),(Join-Path $desktop 'build_world.py'),
    (Join-Path $desktop 'install.py'),(Join-Path $desktop 'preflight.py'),(Join-Path $desktop 'desktop.py'),
    (Join-Path $desktop 'convert.py'),(Join-Path $desktop 'package_layout.py'),
    (Join-Path $desktop 'pursuit.py'),(Join-Path $desktop 'carbon_sequences.py'),(Join-Path $desktop 'palmont_districts.py'),
    (Join-Path $desktop 'public_night_materials.py'),(Join-Path $desktop 'public_world_effects.py'),
    (Join-Path $desktop 'converter_locale.py'),(Join-Path $desktop 'translations.json'),(Join-Path $desktop 'rollback_app.py'),
    (Join-Path $PSScriptRoot 'prepare_public_breaker_inputs.py'),
    (Join-Path $project 'compiler/source-world/SourceExport.cs'),
    (Join-Path $project 'compiler/source-runtime/cameras_compile.py'),
    (Join-Path $project 'compiler/source-runtime/carbon_camera_plan.py'),
    (Join-Path $project 'compiler/source-runtime/curve_oracle.py'),
    (Join-Path $desktop 'assets/world-city-coverage.w2g'),
    (Join-Path $project 'compiler/source-runtime/attributes_compile.py'),
    (Join-Path $project 'compiler/source-runtime/prop_compile.py'),
    (Join-Path $project 'compiler/source-runtime/sequence_plan.py'),
    (Join-Path $project 'compiler/source-runtime/sequences_compile.py'),
    (Join-Path $project 'compiler/source-runtime/mw_prop_fallback.py'),
    (Join-Path $project 'compiler/source-runtime/native_utilities.py'),
    (Join-Path $project 'compiler/source-runtime/native_sky.py'),
    (Join-Path $project 'compiler/source-runtime/bundle.py'),
    (Join-Path $project 'compiler/source-runtime/native_textures.py'),
    (Join-Path $project 'compiler/source-runtime/barriers_compile.py'),
    (Join-Path $project 'compiler/source-runtime/sequences_compile.py'),
    (Join-Path $project 'compiler/source-runtime/toll_compile.py'),
    (Join-Path $project 'compiler/source-runtime/terrain_compile.py'))){
    $rows[(Split-Path $path -Leaf)]=(Get-FileHash -LiteralPath $path).Hash
}
[System.IO.File]::WriteAllText($identity, ($rows | ConvertTo-Json))

$data=@(
    "$desktop/components-catalog.json;.", "$desktop/optional/AddOn;optional/AddOn",
    "$desktop/catalog.json;.", "$desktop/translations.json;.", "$rules;.", "$identity;.",
    "$desktop/assets/radio-selected.png;assets", "$desktop/assets/radio-unselected.png;assets",
    "$desktop/assets/world-city-coverage.w2g;assets",
    "$desktop/presets/SunSet/Config.yml;presets/SunSet",
    "$desktop/bin/W2CMWPublic.asi;bin", "$repacker;bin", "$vault;bin", "$rollback;bin",
    "$project/compiler/source-world/Program.cs;importer-source/compiler/source-world",
    "$project/compiler/source-world/SourceExport.cs;importer-source/compiler/source-world",
    "$project/compiler/source-world/SourceWorld.csproj;importer-source/compiler/source-world",
    "$project/compiler/target-mw/Program.cs;importer-source/compiler/target-mw",
    "$project/compiler/source-world/vendor/Common/Common.csproj;importer-source/compiler/source-world/vendor/Common",
    "$project/compiler/source-world/vendor/CompLib/CompLib.csproj;importer-source/compiler/source-world/vendor/CompLib",
    "$PSScriptRoot/sourceworld.lock.json;importer-source/tools"
)
$args=@(
    '--noconfirm','--noupx','--onedir','--windowed','--name','W2C-to-MW-Converter',
    '--distpath',(Join-Path $build 'portable'),'--workpath',(Join-Path $build 'pyinstaller-clean'),'--specpath',$build,
    '--paths',(Join-Path $project 'compiler/source-runtime'),'--paths',(Join-Path $project 'compiler/source-world'),
    '--paths',$PSScriptRoot,'--exclude-module','curve_oracle',
    '--exclude-module','native_string_hash'
)
foreach($entry in $data){$args+=@('--add-data',$entry)}
& $python -m PyInstaller @args (Join-Path $desktop 'desktop.py')
if($LASTEXITCODE){throw 'Unable to build the portable converter'}
& $python (Join-Path $PSScriptRoot 'build_public_camera_decoder.py') (Join-Path $build 'python-3.12.9-embed-amd64.zip') (Join-Path $build 'portable/W2C-to-MW-Converter/_internal/camera-decoder')
if($LASTEXITCODE){throw 'Unable to stage the isolated camera decoder'}
$optional=Join-Path $build 'portable/W2C-to-MW-Converter/optional'
New-Item -ItemType Directory -Path $optional -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $desktop 'optional/LBx64/LBx64 W2CtoMW RC3.1.zip'),(Join-Path $desktop 'optional/LBx64/README-LBx64.txt'),
    (Join-Path $desktop 'optional/VCache/NFSMWVCacheAffinity 0.7.1.zip'),(Join-Path $desktop 'optional/VCache/README-VCacheAffinity.txt') -Destination $optional -Force
