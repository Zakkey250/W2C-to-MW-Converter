# W2C to MW 2.0.0 — Converter RC4 Preview

[日本語](README.ja.md)

Drive through Rockport and Palmont in **Need for Speed: Most Wanted (2005)**. This Windows x64 tool converts the original [W2C (World to Carbon) 2.0.0](https://nfsmods.xyz/mod/3041) files on your PC and can install the resulting world in a separate Most Wanted game folder. This is the first public release of the converter.

> **Do not redistribute conversion output.** Generate your own pack from the original files. See [Redistribution](#redistribution) below.

## Features

- Choose the original W2C **NORMAL**, **WORLD**, or **X360** quality. The world map and radar use W2C map data. A separate Need for Speed World installation is unnecessary.
- Rebuild Rockport tollbooths, Palmont safehouses and shops, roadside objects, and supported world animations and effects. At shared Palmont facilities, press **Home** to switch between the safehouse and shop, then use the game's normal confirmation control to enter.
- Use optional Carbon assets to restore supported Carbon pursuit breakers, debris, proximity-triggered pursuit cameras, and environmental effects. With the compatible Carbon tools, Palmont place names can distinguish 22 districts. Without Carbon, compatible Most Wanted assets are used where available.
- Check source files and the game folder before converting. Save a verified pack for later or install it directly. Installation does not edit standard saves or write to `GLOBAL/attributes.bin`, `fe_attrib.bin`, `gameplay.bin`, or `gameplay.lzc`. The converter and the standalone rollback tool have 17 interface languages.

## Requirements

**Required**

- Windows x64 and a PC installation of **Need for Speed: Most Wanted (2005)**, with **EN-US 1.3 `speed.exe` prepared with [NFSPatcher](https://github.com/KilLo445/NFSPatcher) and the [4GB Patch](https://ntcore.com/4gb-patch/)** (Large Address Aware). The converter checks the executable's SHA-256. It does not supply or patch `speed.exe`.
- The verified **Redux 3.04** `speed.exe` is also supported. Keep its bundled loader and graphics files. If the game you are installing into has altered world resources, select a separate clean Most Wanted folder as the read-only reference.
- The extracted, unmodified [W2C (World to Carbon) 2.0.0](https://nfsmods.xyz/mod/3041) archive. You can use the extracted source directly; you do not have to install W2C into Carbon first.
- [Ultimate ASI Loader 10.0.2](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v10.0.2), **x86/Win32 `dinput8.dll`**, next to `speed.exe`. The converter also recognizes the verified 6.4.0 build. It does not replace the loader.
- [Microsoft DirectX End-User Runtimes (June 2010)](https://www.microsoft.com/en-us/download/details.aspx?id=8109) with the Win32 `d3dx9_43.dll`, and at least **62 GiB free** on the output drive for temporary conversion files.

**Recommended for graphics**

- [SunSet 1.15.2](https://nfsmods.xyz/mod/6659) for the day/night cycle, weather and lighting. When using SunSet, **[TexWizard 1.3 for MW (`TexWizard_MW.zip`)](https://github.com/R-033/texwizard/releases/tag/1.3)** and SunSet's shaders and texture pack are required. If the check reports missing Win32 Visual C++ runtime files, install the [Microsoft Visual C++ Redistributable (x86)](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
- Download these mods from their original pages. Select the ZIPs in the converter to set up missing files and apply the included W2C preset to a new SunSet installation. Set up SunSet **before converting** to include supported nighttime window lighting. Enable **Shadow Detail** in the game for daytime world shadows. The base conversion works without SunSet. SunSet's `exhaust.nfsms` is for optional exhaust effects; it is not needed for the world and the converter does not apply it.

**Optional**

- A compatible PC installation of **Need for Speed: Carbon**. For Carbon-specific breakers, cameras and effects, extract [OGVI 1.4](https://nfsmods.xyz/mod/5290) over [Attribulator 2.0](https://github.com/NFSTools/Attribulator/releases/tag/v2.0.0) and select that tool folder in the converter.
- [Unlimiter](https://github.com/nlgxzef/NFSMWUnlimiter/releases) and [Extra Options](https://github.com/ExOptsTeam/NFSMWExOpts/releases) are detected if installed. The base conversion does not require them or change their settings. Event Loader, Event Editor and Save Virtualizer are not prerequisites. Save Virtualizer and its save sidecars are not included.

## Install and remove

1. Close the game and its launcher. Back up your saves and use a separate Most Wanted game folder without another world loader.
2. In **Releases → Assets**, download the Windows x64 converter ZIP. Extract it fully, keep `_internal` beside `W2C-to-MW-Converter.exe`, and open the EXE.
3. Select the W2C source, the Most Wanted folder and a quality. Add the optional inputs you want to use and complete the game folder checks.
4. Choose a new output folder and convert. Select automatic installation or use **Install an existing pack** later. Copying an output pack into the game folder by itself does not activate it. After installation, launch that folder's `speed.exe`.
   - At the first conversion, accept the one-time importer setup. The converter verifies and downloads the pinned upstream source and a temporary .NET SDK if needed, then builds the importer locally. Internet access is needed for setup; Python and Visual Studio are not required. [Build instructions](BUILD.md)
5. To restore the previous world, close the game and confirm in `W2CMWPublic/W2CMW-Rollback.exe` inside the game folder. Rollback keeps your saves, converted packs, SunSet and TexWizard.

## Redistribution

**Redistribution of converted output is prohibited.** Do not upload, mirror, share, or bundle whole or partial converted packs, extracted assets, or preinstalled game folders containing conversion output. This also applies to modpacks and collections that bundle the generated files. This release distributes the converter; share the release page so others can run it locally with their own source files. The [original W2C distribution rules](https://nfsmods.xyz/mod/3041) and the terms of each third-party component still apply.

## Known issues and limits

- Some stock Most Wanted events may have checkpoints that cannot be crossed normally. World or event transitions can occasionally take a long time.
- Without Carbon and OGVI, some Carbon-specific breakers, pursuit markers, cameras and environmental effects are omitted. Some objects use Most Wanted substitutes, keep only their appearance, or remain absent when no compatible asset is available. Check the conversion report for substitutions and omissions. Moving props do not cast their own shadows.
- A brief cluster of stutters was observed in one X360 long-distance run. It did not recur on the same section of the repeat run; its cause is unknown. Brief flicker on illuminated windows has also been reported; the release checks did not establish that it was resolved.
- A crash was previously observed in a custom eight-car Hot Pursuit with a weapon mod. That combination was not revalidated for this release. The eight-car sprint checks used no police or weapons; all eight cars physically crossed the finish, but the native results screen was not observed within the test window.
- An incomplete SunSet installation can cause a crash on entering free roam. If you use SunSet, finish the SunSet/TexWizard checks before installing. Mods that replace the same world files may conflict. If you change the game's attribute data after converting, rebuild the pack for that installation.

When reporting a problem, include the selected quality, whether you used Carbon/OGVI and SunSet, the mods installed, and the location or steps to reproduce it. Please do not attach converted packs, extracted game assets or saves.

## Credits

- **W2C to MW Converter:** Zakkey250 (Zakkey76 on Nexus Mods), project direction, integration and testing.
- **Original [W2C (World to Carbon)](https://nfsmods.xyz/mod/3041):** MaxHwoy, with rx, RaTT, 379Felipe, r033, mzry, Crossy, avail, konigsegg, David and heyitsleo. See the linked original project for the full contributor roles and download.
- **Game and original assets:** Electronic Arts and the original development teams behind Need for Speed: Most Wanted, Carbon and World. This is an unofficial fan project.
- **Related tools:** ThirteenAG ([Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)); ARCHIE ([SunSet](https://nfsmods.xyz/mod/6659), [OGVI](https://nfsmods.xyz/mod/5290)); R-033 ([TexWizard](https://github.com/R-033/texwizard)); NFSTools ([Attribulator](https://github.com/NFSTools/Attribulator), [VaultLib](https://github.com/NFSTools/VaultLib), [NFS-ModTools](https://github.com/NFSTools/NFS-ModTools)); KilLo445 ([NFSPatcher](https://github.com/KilLo445/NFSPatcher)); NTCore ([4GB Patch](https://ntcore.com/4gb-patch/)).
- **Libraries and development references:** [NFSPluginSDK](https://github.com/berkayylmao/NFSPluginSDK), [MinHook](https://github.com/TsudaKageyu/minhook), [LZ4](https://github.com/lz4/lz4), [Unicorn](https://github.com/unicorn-engine/unicorn), and [NFSMW reverse engineering notes](https://github.com/s-b-repo/nfsmw-re). Also see the third-party notices in the download.

## AI disclosure

OpenAI Codex (GPT-6) was used to generate and revise code in the converter, rollback tool and game runtime, and to help prepare interface translations and this description. A Fable 5.2 proposal informed the initial redesign. The project owner set the requirements and tested the result. The converter runs locally and does not call an AI service.
