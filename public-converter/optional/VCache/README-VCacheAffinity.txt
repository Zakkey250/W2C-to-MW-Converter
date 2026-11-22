NFSMWVCacheAffinity 0.7.1

日本語

L3キャッシュ容量の異なるCCDを持つAMD Ryzen X3Dが対象です。対応する英語版1.3のspeed.exeが必要です。通常はRC6の「変更内容の確認」で「NFSMWVCacheAffinityをインストール」を選んでください。

手動で導入する場合はゲームとランチャーを終了し、このガイドと同じフォルダーのZIPを展開してください。既存の同名ファイルを退避してから、ZIP内のscriptsとLICENSESをゲームフォルダーにコピーします。すでに旧版がある場合は、重複するASIをゲームフォルダー外へ退避します。既存の設定値は必要に応じて新しいINIへ移してください。

既定ではゲームを3D V-Cache側に割り当てます。LBx64を併用すると、起動時に検出した資源ホストとDLSS補助プロセスを高クロック側に割り当てます。設定と各項目の説明はscripts/NFSMWVCacheAffinity.iniにあります。F2でFPS表示を切り替えます。

コンバーターで導入した場合は、ゲームを終了し、W2CMWPublic/W2CMW-Rollback.exeで復元できます。手動で導入した場合は、追加したファイルを取り除き、退避したファイルを戻してください。

English

This component is for AMD Ryzen X3D CPUs with unequal L3 cache sizes across CCDs. It requires a compatible English 1.3 speed.exe. To install it, select Install NFSMWVCacheAffinity on the RC6 review screen.

For manual installation, close the game and its launcher and extract the ZIP beside this guide. Back up matching files, then copy scripts and LICENSES from the ZIP into the game folder. Move any duplicate older ASI out of the game folder. Copy any custom values you want to keep into the new INI.

The default assigns the game to the 3D V-Cache CCD. With LBx64, the resource host and DLSS worker detected at startup are assigned to the higher-clocked CCD. Settings and descriptions are in scripts/NFSMWVCacheAffinity.ini. Press F2 to toggle the FPS counter.

For converter-managed installation, close the game and run W2CMWPublic/W2CMW-Rollback.exe to restore your previous installation. For manual installation, remove files added by this component and restore your backups.
