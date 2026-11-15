# W2C to MW Converter 2.0.0

**Converter RC5.** Extract the ZIP and open `W2C-to-MW-Converter.exe`. At the first conversion, approve the on-screen importer setup. This uses the internet to fetch and verify the pinned dependencies, then builds the importer locally. Python and Visual Studio are not required.

An existing public RC4 installation can use the separate [RC4 to RC5 Patch](https://github.com/Zakkey250/W2C-to-MW-Converter/releases/tag/v2.0.0-rc4-to-rc5-patch). If it fails, use this converter to rebuild from the original W2C files.

1. Extract [W2C 2.0.0](https://nfsmods.xyz/mod/3041). Leave the original files in place.
2. Prepare a separate Most Wanted game folder with a compatible MW 1.3 LAA (32-bit) `speed.exe` such as the verified NFSPatcher or Redux 3.04 build, a supported [Win32 Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) and Win32 D3DX9. Close the game before installing.
3. Open the converter, choose one of 17 display languages in the top right, and select the W2C folder, game folder and source quality. NORMAL, WORLD and X360 use the corresponding original W2C quality.
4. Review the game folder checks. [SunSet](https://nfsmods.xyz/mod/6659) is recommended for lighting, weather and shadows. If using SunSet, install the matching TexWizard 1.3. The converter links to both downloads and can set up the original ZIPs you select.
5. Review the changes and select a new output folder. You can save a verified pack or install it immediately. If you save only, use **Install an existing pack** to apply it later. Use **Close** on the completion screen.

Carbon is optional. Leave its folder blank for a base conversion, even if an OGVI folder was selected. The converter reads usable MW and Carbon resources regardless of language. For Carbon-specific environment effects, pursuit breakers and their cameras, select the original Carbon game folder and an [OGVI 1.4](https://nfsmods.xyz/mod/5290) installation over [Attribulator 2.0](https://github.com/NFSTools/Attribulator/releases/tag/v2.0.0). Without those inputs, W2C world animation still uses W2C placements, and compatible environment effects and movable props use MW assets. The completion screen lists omitted effects and substitutions. You do not need a separate Need for Speed World installation.

Without Carbon, fuel pumps keep their W2C locations and use the stock MW pump model and explosion. Gas stations and tire shops use matching MW structures where available; parts with no match retain their W2C appearance.

At a shared Palmont facility, press Home to switch between the safehouse and shop. Use the game's normal confirmation control to enter. The safehouse can also be entered during pursuit cooldown.

Reconvert from the original W2C files to apply the updated pursuit breaker blast profiles and roadblock barricade model.

Conversion happens locally. The converter keeps the game's original `GLOBAL/attributes.bin` and saves in place. It uses the selected game files to build the world and keeps backups of replaced files under `W2CMWPublic`. Start the installed world with the selected folder's `speed.exe`.

If the target already has a modified world, select a second, unmodified MW folder as the read-only resource source. The target keeps its cars, attribute banks and saves. The rollback utility restores the exact world files from before installation.

Redux 3.04 can use its verified bundled `speed.exe` and `dinput8.dll`. When copying a Redux installation, include its NextGenGraphics and Custom Shaders modules and their shader and texture files. The converter lists any required files it cannot find.

To restore the original MW world, close the game and confirm the restore in `W2CMWPublic/W2CMW-Rollback.exe` inside the game folder. Saved games, converted packs, SunSet and TexWizard are kept.
