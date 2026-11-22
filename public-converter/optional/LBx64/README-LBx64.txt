LBx64 W2CtoMW RC3.1

日本語

W2CtoMW用の任意コンポーネントです。DLSS Super Resolutionは低い内部解像度から画面解像度へ再構成し、DLAAは画面と同じ解像度でアンチエイリアスを行います。

導入済みの変換パックに追加する場合は、ゲームを終了し、RC6の「変換済みパックを配置」で「LBx64 RC3.1をインストール」を選んでください。既存ファイルの退避と復元をコンバーターが管理します。以下は手動で導入する場合の手順です。

1. W2CtoMWを導入し、ゲームを終了します。DLSS・DLAAには対応するGeForce RTX、NVIDIAドライバ、ASIローダー、SunSet 1.15.0.0が必要です。対応する英語1.3版speed.exeとSunSet.asiのSHA-256は末尾に記載しています。
2. このガイドと同じ場所にある「LBx64 W2CtoMW RC3.1.zip」を一時フォルダーに展開します。ゲームフォルダーにある同名のファイルをバックアップしてから、ZIP内のフォルダー構成を保って配置してください。既存のINIに独自設定がある場合は設定を統合します。RC3からの更新にはRC3.1のゲーム直下のd3d9.dllを使用します。
3. scripts/NFSMWLimitBreaker.iniの[Renderer] BackendをDX12、[DLSS] Enabledを1にします。NVIDIAアプリのゲームプロファイルでSuper Resolutionのモードを選び、DLAAを使う場合はDLAAを選択してからゲームを起動します。ドライバ側のモデル指定を使う場合は、speed.exeとhost64/LBx64W2CtoMW/LBx64W2CtoMW-DLSS.exeに同じ設定を割り当てられます。
4. 通常はspeed.exeから、Save Virtualizer環境ではその環境のランチャーから起動します。フレーム上限はrenderers/dx12/dgVoodoo.confのFPSLimitで変更できます。既定は60です。

コンバーターで配置した場合は、ゲームを終了し、W2CMWPublic/W2CMW-Rollback.exe で導入前の状態に戻せます。手動で配置した場合は、バックアップしたファイルを復元し、このコンポーネントが追加したファイルを取り除いてください。

English

This optional W2CtoMW component supports DLSS Super Resolution and DLAA. Super Resolution reconstructs a lower-resolution scene at display resolution. DLAA applies anti-aliasing at native resolution.

To add LBx64 to an existing converted pack, close the game, open Install an existing pack in Converter RC6, and select Install LBx64 RC3.1. The converter manages backups and rollback. Follow the steps below for manual installation.

1. Install W2CtoMW and close the game. DLSS and DLAA require a supported GeForce RTX GPU, NVIDIA driver, ASI loader and SunSet 1.15.0.0. The supported English 1.3 game and SunSet hashes are listed below.
2. Extract “LBx64 W2CtoMW RC3.1.zip” next to this guide into a temporary folder. Back up matching files in the game folder and copy the files there with their directory structure intact. Merge any custom INI settings. When upgrading from RC3, use the RC3.1 d3d9.dll in the game root.
3. In scripts/NFSMWLimitBreaker.ini, set [Renderer] Backend to DX12 and [DLSS] Enabled to 1. Choose the Super Resolution mode in the NVIDIA app game profile, or select DLAA for native-resolution anti-aliasing, then start the game. To use driver model overrides, you can assign the same profile settings to speed.exe and host64/LBx64W2CtoMW/LBx64W2CtoMW-DLSS.exe.
4. Start speed.exe, or use the installation's launcher if you use Save Virtualizer. The default frame cap is 60. To change it, edit FPSLimit in renderers/dx12/dgVoodoo.conf.

For a converter-managed installation, close the game and run W2CMWPublic/W2CMW-Rollback.exe to restore the pre-installation state. For a manual installation, restore your backups and remove files added by this component.

SHA-256
speed.exe (English 1.3)   80774C2E5D619B4F120B48D4462896FD504C263399D203A238769CFFDE1D253C
speed.exe (English 1.3 LAA)   B248271BF8EAC8C9B283B8C95E3ADD672B713BF529B05F1780E58268493B9D06
scripts/SunSet.asi   9C1E951687EE521456DA763EB747C60720C7A2D7BFCA1DAAF980C946B6BCC0A3
LBx64 W2CtoMW RC3.1.zip   66F71A5C527A4B6E1A21605FD733DAA526948F18938EF21E65BA2E166DDDE32C
