"""User-facing strings for the Windows converter and rollback utility."""
import ctypes
import json
import locale
from pathlib import Path
import re
from string import Formatter


LANGUAGE_LABELS = {
    "ja": "日本語", "en": "English", "da": "Dansk", "nl": "Nederlands", "fi": "Suomi",
    "fr": "Français", "de": "Deutsch", "it": "Italiano", "ko": "한국어", "pl": "Polski",
    "ru": "Русский", "es": "Español (España)", "es-MX": "Español (México)", "sv": "Svenska",
    "th": "ไทย", "zh-Hans": "简体中文", "zh-Hant": "繁體中文",
}
LANGUAGES = tuple(LANGUAGE_LABELS.values())
ENGLISH = {
    "NFSMWVCacheAffinityをインストール": "Install NFSMWVCacheAffinity",
    "NFSMWVCacheAffinityには対応する英語版1.3と、L3容量が異なるCCDを持つAMD Ryzen X3Dが必要です。": "NFSMWVCacheAffinity requires a compatible English 1.3 executable and an AMD Ryzen X3D with unequal L3 cache sizes across CCDs.",
    "NFSMWVCacheAffinityには対応する英語版1.3の実行ファイルが必要です。": "NFSMWVCacheAffinity requires a compatible English 1.3 executable.",
    "NFSMWVCacheAffinityの同梱ファイルを確認できません。ZIPを展開し直してください。": "The bundled NFSMWVCacheAffinity files could not be verified. Extract a fresh copy of the converter ZIP.",
    "LBx64の導入時は旧LBを退避して差し替えます。DLSS・DLAAには対応するNVIDIA環境とSunSetが必要です。": "Installing LBx64 backs up and replaces the old LB. DLSS and DLAA require a compatible NVIDIA system and SunSet.",
    "Addonのインストール — W2C 未開通区間の暫定開通": "Install Addon — W2C provisional access to unopened roads",
    "LBx64 RC3.1をインストール": "Install LBx64 RC3.1",
    "未選択のコンポーネントも、導入ガイド付きでOptionalフォルダーに保存します。": "Unselected components are also saved in Optional with installation guides.",
    "W2C to MW のアンインストール": "Uninstall W2C to MW",
    "MW原本のワールドに戻す": "Restore the original MW world",
    "このフォルダーをMW原本の状態に戻しますか？": "Restore the original MW world in this folder?",
    "MW原本のワールドに戻しました。": "The original MW world was restored.",
    "このフォルダーに有効なW2Cの配置はありません。": "There is no active W2C installation in this folder.",
    "ゲームを終了してください。MW原本を復元し、W2Cを無効にします。セーブ、変換済みパック、SunSetとTexWizardは保持します。": "Close the game. This restores the original MW world and disables W2C. Saved games, converted packs, SunSet and TexWizard are kept.",
    "ゲームフォルダー内のW2CMWPublicから起動してください。": "Open this utility from W2CMWPublic inside the game folder.",
    "復元する": "Restore",
    "復元用の原本が見つからないか、内容が異なります。{0}": "The original backup is missing or has changed. {0}",
    "配置後にファイルが変更されたため、復元を中止しました。{0}": "A file was changed after installation. Restore cancelled. {0}",
    "Close Most Wanted before applying or recovering an installation.": "Close Most Wanted before installing or restoring.",
    "An installation operation is already in progress.": "Another installation or restore is already in progress.",
    "Unrecognized installation state.": "The installation record is invalid.",
    "原本の選択": "Select source",
    "導入先の検査": "Check game folder",
    "変更内容の確認": "Review changes",
    "変換と配置": "Convert and install",
    "標準の World テクスチャ": "Standard World textures",
    "高品質の World テクスチャ": "High quality World textures",
    "高品質の Carbon・MW テクスチャ": "High quality Carbon and MW textures",
    "MW原本と一致しない小物の移動・当たり判定・破壊": "Motion, collision and destruction for props with no matching MW resource",
    "Carbon固有のパースートブレーカーと専用カメラ": "Carbon pursuit breakers and their cameras",
    "ローカル変換": "Local conversion",
    "参照": "Browse",
    "フォルダーを選ぶ": "Choose a folder",
    "変換元を選ぶ": "Select the source",
    "W2C 2.0.0の展開フォルダーと、使用する原本品質を指定してください。": "Select the extracted W2C 2.0.0 folder and the source quality you want to use.",
    "W2C 2.0.0 原本（必須）": "W2C 2.0.0 source (required)",
    "Most Wanted の導入先（必須）": "Most Wanted game folder (required)",
    "Carbon のゲームフォルダー（任意）": "Carbon game folder (optional)",
    "OGVI 1.4 のフォルダー（Carbonの装置・専用カメラを復元する場合）": "OGVI 1.4 folder (for Carbon effects, breakers and cameras)",
    "原本の品質": "Source quality",
    "変換済みパックを配置": "Install an existing pack",
    "{0}を指定してください。": "Select {0}.",
    "導入先を確認": "Check the game folder",
    "コピー済みのパックを検出": "Copied pack detected",
    "導入先にまだ配置されていない変換済みパックがあります。\nこのパックを照合し、再変換せずに配置しますか？\n\n「いいえ」で別のパックを選べます。": "This folder contains a converted pack that has not been installed.\nVerify and install this pack?\n\nSelect No to choose a different pack.",
    "変換済みパックのフォルダーを選ぶ": "Choose a converted pack folder",
    "Most Wanted の導入先": "Most Wanted game folder",
    "W2C 2.0.0原本": "W2C 2.0.0 source",
    "専用カメラを復元するにはCarbonのゲームフォルダーを指定してください。": "Select the Carbon game folder to restore its dedicated cameras.",
    "入力を確認": "Check your selections",
    "原本と対象の実行ファイル、必要なMODを照合しています。": "Checking the original files, the executable and required mods.",
    "導入先を検査": "Check game folder",
    "配置に必要な項目を確認してください。対応する導入先なら再検査できます。": "Review the installation requirements. You can check the folder again after making changes.",
    "適合": "Supported",
    "{0}件 要確認": "Issues to review: {0}",
    "変換済みパック": "Converted pack",
    "ファイルとSHA-256を照合しました。": "File sizes and SHA-256 fingerprints verified.",
    "要確認": "Review needed",
    "MWの原本資源": "Original MW resources",
    "純正MWの参考元": "Unmodified MW resource folder",
    "純正MWの参考元（導入先のワールドが改造済みの場合）": "Unmodified MW resource folder (for an existing modified world)",
    "純正MWのフォルダーを参考元として指定してください: {0}": "Select an unmodified MW resource folder: {0}",
    "導入前のMW資源を確認してください: {0}": "Check the original target resource: {0}",
    "導入先のMW資源が変換中に変更されました。": "Target MW resources changed during conversion.",
    "導入前のMW資源が変換時と異なります。再変換してください。": "The target world differs from the original conversion. Convert again.",
    "改造済みの場合は {0} も退避・置換": "If modified, {0} is also backed up and replaced",
    "対応する speed.exe を使用してください（{0}）。": "Use a supported speed.exe ({0}).",
    "Redux 3.04と一致。": "Matches the executable bundled with Redux 3.04.",
    "{0} 同梱の32ビット dinput8.dll を照合しました。": "Verified the 32-bit dinput8.dll bundled with {0}.",
    "Redux 3.04の同梱描画資源を復元してください。不足したまま起動すると3D画面が黒くなる場合があります。": "Restore the graphics files bundled with Redux 3.04. Missing files may cause a black 3D view.",
    "Redux 3.04の同梱描画資源を確認しました。": "Found the graphics files bundled with Redux 3.04.",
    "Carbon原本とOGVIを使って復元します。": "Restores the original Carbon assets using OGVI.",
    "有効": "Enabled",
    "パースートブレーカーと専用カメラ": "Pursuit breakers and their cameras",
    "復元する場合はOGVI 1.4を指定してください。このまま進むとCarbonの画像資源だけを補います。": "Select OGVI 1.4 to restore Carbon effects, breakers and cameras. Without it, Carbon supplies compatible textures only.",
    "OGVI未指定": "OGVI not selected",
    "OGVI 1.4の配布元": "Get OGVI 1.4",
    "{0}件 要対応": "Issues to resolve: {0}",
    "{0} の配布元": "Get {0}",
    "確認済み": "Verified",
    "Win32 ASIローダー・D3DX": "Win32 ASI Loader and D3DX",
    "\nWin32 D3DXを検出しました。": "\nWin32 D3DX detected.",
    "導入済み": "Installed",
    "未導入": "Not installed",
    "配置前に要対応": "Required before installation",
    "SunSet（導入する場合はTexWizard必須）": "SunSet (requires TexWizard when installed)",
    "SunSetの配布元": "Get SunSet",
    "TexWizard 1.3の配布元": "Get TexWizard 1.3",
    "SunSetの推奨設定を開く": "Open recommended SunSet settings",
    "SunSet・TexWizardを自動セットアップ": "Set up SunSet and TexWizard",
    "ワールドの影": "World shadows",
    "ゲーム内で確認": "Check in game",
    "任意MOD　": "Optional mods: ",
    "コピー済みパックを配置": "Install copied pack",
    "中断した配置を復元": "Recover interrupted installation",
    "再検査": "Check again",
    "SunSetのセットアップ": "SunSet setup",
    "各配布元から入手したZIPを選んでください。すでに導入済みのMODは選択を省けます。": "Select the ZIPs downloaded from the links below. Skip mods you already have installed.",
    "{0} の配布元を開く": "Open {0} download page",
    "{0} のZIPを選択": "Select the ZIP for {0}",
    "ZIPを選択": "Select ZIP",
    "{0}\n不足している描画資源とMODを配置します。新規導入ではW2C用の設定を使います。既存の設定は保持し、必要な場合だけTexWizardにSunSetのパック参照を追加します。ゲームを終了してから進んでください。": "{0}\nInstalls missing rendering resources and mods. New installations use W2C settings. Existing settings are kept and the SunSet pack is added to TexWizard when needed. Close the game before continuing.",
    "配置先と変更内容": "Destination and changes",
    "導入済みのSunSetでも白飛びを抑える（Bloomをオフ）": "Reduce glare in an existing SunSet install (turn Bloom off)",
    "SunSetをセットアップ中": "Setting up SunSet",
    "ZIPと導入先の資源を照合し、不足分を配置しています。": "Checking the ZIPs and existing files, then installing missing resources.",
    "不足分を配置": "Install missing files",
    "配置の復元": "Installation recovery",
    "中断した配置を記録に従って復元しますか？": "Restore the interrupted installation using its journal?",
    "配置を復元中": "Recovering the installation",
    "変更記録と退避データを照合しています。": "Checking the journal and backups.",
    "変換済みパックの配置先を確認してください。": "Review the destination for your converted pack.",
    "CarbonとW2C原本から、動く小物、パースートブレーカー、専用カメラを生成します。": "Restore moving props, pursuit breakers, cameras and environment effects from Carbon and W2C.",
    "MW原本と一致する小物は接触で動きます。給油機はMWのモデルと爆発で代替し、Carbon固有の演出と専用カメラは省略します。": "Matching MW props react to contact. Fuel pumps use MW models and explosions. Carbon-specific effects and cameras are omitted.",
    "変更内容を確認": "Review changes",
    "ワールドマップとレーダー": "World map and radar",
    "W2C原本の輪郭地図を使用。": "Uses the original W2C contour map.",
    "原本": "Original",
    "Carbon 資源と装飾": "Carbon resources and props",
    "照合済み": "Verified",
    "W2C・MW原本": "W2C and MW originals",
    "検出したMODの設定": "Settings for detected mods",
    "手動確認": "Review manually",
    "{0}\n退避先は {2}/originals と {4}/rollback。": "{0}\nBackups: {2}/originals and {4}/rollback.",
    "配置先と復元情報": "Destination and recovery",
    "配置時": "On installation",
    "出力先（新しく作るフォルダー）": "Output folder (will be created)",
    "変換後に、選択したMWフォルダーへ自動配置": "Install automatically to the selected MW folder after conversion",
    "オフの場合は保存のみ。ゲームへ反映するには配置が必要です。": "When disabled, the pack is saved only. Install it to use it in game.",
    "戻る": "Back",
    "パックを配置": "Install pack",
    "ローカル変換を開始": "Start local conversion",
    "{0}/generations/（ワールドパック）": "{0}/generations/ (world pack)",
    "{0}\n\n{2}\n\n変更前のファイルは {4} 内に退避されます。ゲームを閉じて続行してください。": "{0}\n\n{2}\n\nThe original files will be backed up in {4}. Close the game before continuing.",
    "配置先を確認": "Confirm destination",
    "出力先を確認": "Check the output folder",
    "新しい出力フォルダーを指定してください。": "Choose a new output folder.",
    "ローカル変換中": "Converting locally",
    "検証が終わるまでお待ちください。": "Wait for the validation to finish.",
    "原本の照合": "Source verification",
    "完了": "Complete",
    "変換を準備": "Preparing conversion",
    "変換を中止": "Cancel conversion",
    "検証済みパックを配置中": "Installing the verified pack",
    "配置先の状態を再検査し、変更前のファイルを退避してから適用します。": "Checking the destination again and backing up files before installing.",
    "配置が完了しました": "Installation complete",
    "変換済みパックを保存しました（未配置）": "Converted pack saved (not installed)",
    "Most Wanted の speed.exe から起動できます。": "Launch Most Wanted using speed.exe.",
    "ゲームには反映されていません。この画面から配置できます。": "The game has not changed. You can install the pack from this screen.",
    "ゲームには反映されていません。導入先を再検査してから配置してください。": "The game has not changed. Recheck the game folder before installing.",
    "導入先": "Game folder",
    "配置済み": "Installed",
    "ワールドパック": "World pack",
    "検証済み": "Verified",
    "空・照明・反射など {1} 種を補いました。": "Restored {1} textures, including sky, lighting and reflections.",
    "Carbon 資源": "Carbon resources",
    "使用済み": "Used",
    "物理配置 {1}件、専用カメラ {3}シーンを生成。原本モデル欠落 {5}件。": "Physics placements: {1}. Camera scenes: {3}. Placements without source models: {5}.",
    "W2C原本から物理属性を{1}件補完。": "Recovered {1} physics attributes from the W2C source.",
    "パースートブレーカーと小物": "Pursuit breakers and props",
    "本体・遮断機・標識など {1}件を復元。": "Restored {1} items, including booths, barriers and signs.",
    "MWの料金所": "MW toll booths",
    "ワールドアニメーションと環境エフェクト": "World animation and environment effects",
    "動く構造物 {1}件、テクスチャアニメーション {3}件、環境エフェクト {5}件。\nMWの類似エフェクトで代替 {7}件。資源がなく省略 {9}件。": "Moving world objects: {1}. Texture animations: {3}. Effect placements: {5}.\nMW substitutes: {7}. Omitted for lack of matching resources: {9}.",
    "生成済み": "Generated",
    "省略された環境エフェクト": "Omitted environment effects",
    "未解決の地形テクスチャ {1}種、モデルが見つからない配置 {3}件。\nMW資源で動く小物を {5}件復元。別の小物 {7}件は外観のみ再現。動的な当たり判定・破壊は {9}件が未変換です。": "Unresolved terrain textures: {1}. Placements without models: {3}.\nMoving props restored from MW: {5}. Other props restored as static scenery: {7}. Placements without dynamic collision or destruction: {9}.",
    "資源の省略": "Missing resources",
    "確認": "Review",
    "再現できていない機能": "Remaining limitations",
    "・": "• ",
    "未対応": "Unavailable",
    "導入先を開く": "Open game folder",
    "出力フォルダーを開く": "Open output folder",
    "配置を再試行": "Retry installation",
    "このパックを配置": "Install this pack",
    "入力へ戻る": "Back to inputs",
    "閉じる": "Close",
    "処理を完了できませんでした": "Could not finish",
    "現在の処理が終わり次第、中止します。": "Cancellation will take effect after the current operation.",
    "TexWizard_MW.zip（1.3）": "TexWizard_MW.zip (1.3)",
    "TexWizard.json がありません。": "TexWizard.json is missing.",
    "TexWizard.json の形式を確認してください。": "Check the format of TexWizard.json.",
    "SunSetの Config.yml を確認してください。": "Check the SunSet Config.yml file.",
    "指定品質のW2C 2.0.0原本を展開してください。": "Extract the original W2C 2.0.0 files for the selected quality.",
    "原本とファイルサイズが一致しません。": "The file size does not match the original.",
    "選択した品質・版の原本ハッシュと一致しません。": "The fingerprint does not match the original for the selected quality and version.",
    "W2C 2.0.0原本と一致。": "Matches the W2C 2.0.0 original.",
    "Most Wantedの導入先を指定してください。": "Select the Most Wanted game folder.",
    "NFSPatcher製 EN-US 1.3 LAAの実行ファイルが必要です。": "An NFSPatcher EN-US 1.3 LAA executable is required.",
    "対応する実行ファイルのSHA-256と一致しません。": "The executable SHA-256 does not match the supported build.",
    "NFSPatcher EN-US 1.3 LAAと一致。": "Matches NFSPatcher EN-US 1.3 LAA.",
    "Ultimate ASI Loader {1} (Win32 dinput8.dll) を照合しました。": "Verified Ultimate ASI Loader {1} (Win32 dinput8.dll).",
    "32ビットの dinput8.dll を検出しましたが、検証済みビルドと一致しません。": "Found a 32-bit dinput8.dll, but it does not match a verified build.",
    "dinput8.dll が有効な32ビットDLLとして認識されません。": "dinput8.dll is not a valid 32-bit DLL.",
    "version.dll を検出しました。確認版の配置には検証済みの dinput8.dll が必要です。": "Found version.dll. This preview requires a verified dinput8.dll.",
    "ゲームフォルダーに検証済みの32ビット dinput8.dll がありません。": "No verified 32-bit dinput8.dll found in the game folder.",
    " 検証済み: Ultimate ASI Loader 6.4.0 / 10.0.2。": " Verified versions: Ultimate ASI Loader 6.4.0 and 10.0.2.",
    "照明・天候・影の再現には導入を推奨します。未導入のままでも配置できます。": "Recommended for lighting, weather and shadows. Installation can proceed without it.",
    "SunSet.asi が複数あります。使うファイルを1つにして再検査してください。": "Multiple copies of SunSet.asi were found. Keep one active copy and check again.",
    "SunSet.asi を有効な32ビット版として確認できません。": "SunSet.asi is not a valid 32-bit build.",
    "検証済みのWin32 TexWizard 1.3（1個）": "One verified Win32 copy of TexWizard 1.3",
    "TexWizardのSunSetパック参照": "SunSet pack reference in TexWizard",
    "有効なTexWizard.json": "Valid TexWizard.json",
    "SunSetに必要なファイルがそろっていません。自動セットアップで不足を補ってください。\n": "SunSet files are missing. Run the setup to install them.\n",
    "SunSet.asi が検証済みの公式配布ZIP {0} と一致しません。": "SunSet.asi does not match the verified official ZIP {0}.",
    "公式配布ZIP {0} のSunSet、TexWizard 1.3、描画資源を確認しました。": "Verified SunSet from the official ZIP {0}, TexWizard 1.3 and rendering resources.",
    "SunSetとTexWizardを導入し、MWの画面設定で「Shadow Detail（影の詳細）」を有効にしてください。同梱設定では夜間の影を無効にしています。動く小物自身の影は未対応です。": "Install SunSet and TexWizard, then enable Shadow Detail in the MW video settings. The included preset disables night shadows. Moving props do not cast their own shadows.",
    "検出": "Detected",
    "Win32 D3DXを検出。": "Win32 D3DX detected.",
    "Win32 D3DXを導入、またはゲーム内の候補DLLを確認してください。": "Install Win32 D3DX or check the DLL in the game folder.",
    "既存のワールドローダーを検出。独立した導入先を選んでください。": "Another world loader was found. Select a separate game folder.",
    "検出。対応版と必要な設定は適用前に確認します。": "Detected. Version and required settings are checked before installation.",
    "EN-US環境で起動時に停止する場合は、[MAIN] の SkipIntro を 0 にして再起動してください。": "If the EN-US game stops at launch, set SkipIntro to 0 in [MAIN] and restart.",
    "TexWizardの設定を解釈できません。導入先のTexWizard設定を確認してください。": "Could not read TexWizard settings. Check the TexWizard configuration in the game folder.",
    "存在しないパック参照を確認し、退役済みのパックだけを packs から外してください。": "Check the missing pack references and remove only retired packs from the packs list.",
    "未指定。W2CとMWの原本で変換します。": "No Carbon folder selected. Using the original W2C and MW assets.",
    "照合済みのCarbon資源から、W2C原本にない空・照明・反射などのテクスチャを補います。": "Uses verified Carbon textures for missing W2C sky, lighting and reflection assets.",
    "対応するCarbon資源を検出できませんでした。W2CとMWの原本で変換します。": "No compatible Carbon assets were found. Using the original W2C and MW assets.",
    " 未検出または版が異なるバンクは変換に使いません。": " Missing or incompatible banks are skipped.",
    "フォルダーが見つかりません: {1}": "Folder not found: {1}",
    "原本を読み込む": "Reading original files",
    "原本のモデル・テクスチャを照合": "Verifying original models and textures",
    "ワールドパックを組み立てて検証": "Assembling and verifying the world pack",
    "検証済みパックを保存": "Saving the verified pack",
    "パースートブレーカーの復元にはMW原本の {1} が必要です。": "Restoring pursuit breakers requires the original MW file {1}.",
    "選択したゲームの物理属性ファイルが見つからないか、形式が異なります。GLOBAL/{1}": "Game attributes are missing or unsupported: GLOBAL/{1}",
    "変換中に物理属性ファイルが変わりました。GLOBAL/{1}": "Game attributes changed during conversion: GLOBAL/{1}",
    "W2C 2.0.0の小物属性スクリプトが原本と一致しません。": "The W2C 2.0.0 prop attribute script does not match the original.",
    "W2C原本から復元した小物属性を照合できませんでした。": "Could not verify the prop attributes recovered from W2C.",
    "MW原本とパースートブレーカーの物理属性を照合": "Verifying original MW and pursuit breaker physics",
    "当たり判定・破片・専用カメラを生成": "Building collision, debris and dedicated cameras",
    "導入先に未配置の変換済みパックが見つかりません。": "No converted pack awaiting installation was found in the game folder.",
    "指定したMODのフォルダーに必要なファイルがありません: {1}": "Required file missing from the mod folder: {1}",
    "指定したMODのZIPに必要なファイルがありません: {1}": "Required file missing from the mod ZIP: {1}",
    "SunSetの配置には50 GiBの予備領域に加えて384 MiBの空きが必要です。": "SunSet setup needs 384 MiB in addition to the 50 GiB free-space reserve.",
    "検証済みの公式配布ファイルと一致しません: {1}": "Does not match the verified official file: {1}",
    "指定したMODの資源を確認できません: {1}": "Could not verify the selected mod resource: {1}",
    "Config.yml の Config → Bloom を false に設定してから再検査してください。": "Set Config → Bloom to false in Config.yml, then check again.",
    "Config.yml の Config → Bloom を確認してください。": "Check Config → Bloom in Config.yml.",
    "\n既存のMODファイルは上書きしていません.": "\nExisting mod files were not overwritten.",
    "\n既存のMODファイルは上書きしていません。": "\nExisting mod files were not overwritten.",
    "導入先の属性データが変換時と異なります。再変換してください: GLOBAL/{1}": "The game attributes changed since conversion. Convert the pack again: GLOBAL/{1}",
    "コピー済みパックの配置状態が変わりました。導入先を再検査してください。": "The copied pack changed. Check the game folder again.",
    "地形・モデル・テクスチャを生成": "Building terrain, models and textures",
    "MWの原本から動く小物を復元": "Restoring moving props from the original MW assets",
    "MW原本の純正イベントバリアを復元": "Restoring the original MW event barriers",
    "地形ページを生成": "Building terrain pages",
    "SunSetの発光テクスチャ設定を確認してください。": "Check the SunSet emissive texture settings.",
    "変換中にSunSetの発光テクスチャ資源が変わりました。": "SunSet emissive textures changed during conversion.",
    "SunSetの発光マスクの照合に失敗しました。": "Could not verify the SunSet emissive masks."
}
_PLACEHOLDER = re.compile(r"\{(\d+)\}")
CATALOG_PATH = Path(__file__).with_name("translations.json")
TRANSLATIONS = json.loads(CATALOG_PATH.read_text(encoding="utf-8")) if CATALOG_PATH.is_file() else {}


def windows_language():
    if not hasattr(ctypes, "windll"):
        return "en"
    lcid = ctypes.windll.kernel32.GetUserDefaultUILanguage()
    primary = lcid & 0x3ff
    if primary == 4:
        return "zh-Hant" if lcid in (0x0404, 0x0c04, 0x1404, 0x7c04) else "zh-Hans"
    name = locale.windows_locale.get(lcid, "en").replace("_", "-")
    if name in LANGUAGE_LABELS:
        return name
    code = name.split("-", 1)[0].lower()
    return code if code in LANGUAGE_LABELS else "en"


_active = windows_language()


def locale_label():
    return LANGUAGE_LABELS[_active]


def set_locale_label(label):
    global _active
    _active = next(code for code, name in LANGUAGE_LABELS.items() if name == label)


def _catalog():
    if _active == "ja":
        return {key: key for key in ENGLISH}
    return TRANSLATIONS.get(_active, ENGLISH)


def tr(key, *values):
    result = _catalog().get(key, ENGLISH.get(key, key) if _active != "ja" else key)
    return result.format(*values) if values else result


def _detail_pattern(source):
    parts = [re.escape(literal) + (f"(?P<value{field}>[^\u3002\n]+)" if field else "")
             for literal, field, _, _ in Formatter().parse(source)]
    return re.compile("".join(parts))


# Backend messages remain stable for CLI callers. Translate rendered details,
# including embedded paths and file names, at the desktop boundary.
_PATTERNS = tuple((key, _detail_pattern(key)) for key in ENGLISH if _PLACEHOLDER.search(key))


def detail(text):
    if _active == "ja" or not text:
        return text
    catalog = _catalog()
    if text in ENGLISH:
        return catalog.get(text, ENGLISH[text])
    for source, pattern in _PATTERNS:
        if match := pattern.fullmatch(text):
            values = {name[5:]: value for name, value in match.groupdict().items()}
            return _PLACEHOLDER.sub(lambda field: values[field[1]], catalog.get(source, ENGLISH[source]))
    for source in sorted(ENGLISH, key=len, reverse=True):
        if len(source) >= 8 and not _PLACEHOLDER.search(source):
            text = text.replace(source, catalog.get(source, ENGLISH[source]))
    return text if _active.startswith("zh") else text.replace("、", ", ")

