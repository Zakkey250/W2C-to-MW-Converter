# Build the RC4 Preview converter on Windows

The source here covers the converter UI and conversion pipeline, rollback tool,
the world ASI and repacker, the vault helper, and the adapter for the W2C source
importer. It contains no original game installation, pre-converted world,
save files, or prebuilt source importer.

## Build requirements

- Windows x64 and [CPython 3.12.9 x64](https://www.python.org/downloads/release/python-3129/) with Tk and the Windows launcher.
- Visual Studio or Visual Studio Build Tools with **Desktop development with C++**, MSVC **v143** and the Windows 10 SDK. Run the commands below in a Developer PowerShell.
- [Microsoft.DXSDK.D3DX 9.29.952.8](https://www.nuget.org/packages/Microsoft.DXSDK.D3DX/9.29.952.8). Extract the NuGet package from NuGet and use its `build/native/include` directory. The runtime uses system DirectX, not DirectX binaries redistributed by this repository.
- A .NET SDK, with workload support for publishing a self-contained Windows x86 .NET 8 executable. The RC4 helper was built with the .NET 11 SDK and the project pins the x86 .NET 8 runtime to 8.0.29.
- For the vault helper's build references, use [OGVI 1.4](https://nfsmods.xyz/mod/5290) extracted over [Attribulator 2.0](https://github.com/NFSTools/Attribulator/releases/tag/v2.0.0). The linked assemblies remain in your tool folder and are not in the converter ZIP.

```powershell
git clone --branch v2.0.0-rc4 https://github.com/Zakkey250/W2C-to-MW-Converter.git
cd W2C-to-MW-Converter
git clone https://github.com/berkayylmao/NFSPluginSDK.git third_party/NFSPluginSDK
git -C third_party/NFSPluginSDK checkout d238bdffc648498840d17133edb256fa0310d355
msbuild compiler/native/Repack.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild runtime/game-mw13/W2CMWv2.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:W2CPublic=true "/p:D3DXInclude=C:/deps/d3dx/build/native/include"
.\tools\Build-PublicConverterDesktop.ps1 -VaultToolReference "C:/tools/ogvi-attribulator"
```

The portable output is in `build/desktop/portable/W2C-to-MW-Converter`.
The script builds the two Python applications (PyInstaller), publishes the
vault helper, and prepares the isolated camera interpreter. It intentionally
does not build or package SourceWorld. The portable EXE creates that dependency
when the user first confirms a conversion. PyInstaller and the embedded
Python interpreter use standard-library ZIP files. Upstream NFSPluginSDK is
checked out separately by developers; the file with its license accompanies
the binary distribution. Native and self-contained build hashes can differ
across toolchains; the ZIP checksum identifies the published build.

## SourceWorld (first-time setup or manual build)

On first conversion, the portable EXE offers to build SourceWorld and places
the resulting helper in its own `_internal/bin` folder. It downloads two fixed
NFS-ModTools revisions from GitHub and, when the exact .NET SDK is unavailable,
a portable SDK from Microsoft. The SHA-256 or SHA-512 of each download is
verified before use. The SDK is temporary, and setup does not need admin rights
or upload original inputs.

To reproduce this standalone step from the source checkout or diagnose it:

```powershell
py -3.12 tools/setup_sourceworld.py --output "C:/path/to/extracted-converter/_internal/bin/SourceWorld.exe"
```

The recipe in [tools/sourceworld.lock.json](tools/sourceworld.lock.json)
identifies the upstream contents and records the local integration changes.
The C# source in this repository supplies the integration. For iterating on
the build, the setup script also accepts a directory with the two pinned ZIP
files (`--archives`) and an explicit `dotnet.exe` path (`--dotnet`).
Neither the upstream NFS-ModTools source nor the locally built importer is
part of the release asset.
