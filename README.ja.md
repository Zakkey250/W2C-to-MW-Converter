# W2C to MW 2.0.0 — Converter RC5

[English](README.md)

**Need for Speed: Most Wanted（2005）で、ロックポートとパルモントを走ろう。** [W2C（World to Carbon）2.0.0](https://nfsmods.xyz/mod/3041) の原本から、Most Wanted用のワールドを自分のPCで変換するWindows x64向けツールです。変換済みパックの保存と、独立したMost Wanted環境への自動配置ができます。

[コンバーター RC5](https://github.com/Zakkey250/W2C-to-MW-Converter/releases/tag/v2.0.0-rc5) · [RC4→RC5パッチャー](https://github.com/Zakkey250/W2C-to-MW-Converter/releases/tag/v2.0.0-rc4-to-rc5-patch)

公開版RC4を導入済みの場合は、専用パッチャーで更新できます。適用に失敗する場合は、コンバーターRC5でW2C 2.0.0の原本から再構築してください。

> **変換生成物は再配布厳禁です。** 各自で原本から変換してください。詳しくは下記の「再配布について」をご覧ください。

## 主な機能

- W2C原本の **NORMAL／WORLD／X360** の3品質に対応。ワールドマップとレーダーはW2Cの地図データを使います。Need for Speed World本体のインストールは不要です。
- ロックポートの料金所、パルモントのセーフハウスとショップ、道路脇の小物、対応するワールドアニメーションと環境エフェクトを復元します。パルモントの共用施設では **Homeキー**でセーフハウスとショップを切り替え、ゲームの決定操作で入場します。
- 任意のCarbon資源を使うと、対応するパースートブレーカー、破片、追跡中に警察が近くにいるときの専用カメラ、環境エフェクトを復元できます。対応ツールを併用するとパルモントの地名表示を22地区に細分化できます。Carbonがない場合は、利用できるMost Wanted資源で代替します。
- 原本と導入先を検査してから変換。変換済みパックを保存するか、そのまま自動配置できます。配置処理は標準セーブと `GLOBAL/attributes.bin`、`fe_attrib.bin`、`gameplay.bin`、`gameplay.lzc` を書き換えません。コンバーターと独立したロールバッカーは17言語に対応しています。

## 必要なもの

**必須**

- Windows x64と **Need for Speed: Most Wanted（2005）PC版**。[NFSPatcher](https://github.com/KilLo445/NFSPatcher) で用意し、[4GB Patch](https://ntcore.com/4gb-patch/) を適用した **EN-US 1.3 LAA の `speed.exe`** が必要です。コンバーターはSHA-256で照合します。`speed.exe` の同梱やパッチ適用は行いません。
- 検証済みの **Redux 3.04** 同梱 `speed.exe` にも対応します。同梱のローダーと描画資源を維持してください。導入先のワールド資源に改変がある場合は、別の未改造MWフォルダーを読み取り専用の参考元に指定してください。
- [W2C 2.0.0](https://nfsmods.xyz/mod/3041) の未改変の原本一式。配布アーカイブを展開すれば使えます。Carbonへの事前導入は不要です。
- [Ultimate ASI Loader 10.0.2](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v10.0.2) の **x86／Win32版 `dinput8.dll`**。`speed.exe` と同じフォルダーに置きます。照合済みの6.4.0も検出できます。コンバーターはローダーを置き換えません。
- [Microsoft DirectX エンドユーザーランタイム（June 2010）](https://www.microsoft.com/en-us/download/details.aspx?id=8109) のWin32版 `d3dx9_43.dll`。中間ファイル作成のため、出力先ドライブに **62 GiB以上**の空き容量が必要です。

**描画の推奨構成**

- 昼夜サイクル・天候・照明には [SunSet 1.15.2](https://nfsmods.xyz/mod/6659) を推奨します。SunSetを使う場合は **[TexWizard 1.3のMW版（`TexWizard_MW.zip`）](https://github.com/R-033/texwizard/releases/tag/1.3)** とSunSetのシェーダー・テクスチャパックが必須です。Win32のVisual C++ランタイム不足が表示された場合は [Microsoft Visual C++ 再頒布可能パッケージ（x86）](https://learn.microsoft.com/ja-jp/cpp/windows/latest-supported-vc-redist) を導入してください。
- MOD本体は各配布元から入手してください。コンバーターでZIPを指定すると不足ファイルを配置し、新規導入時には同梱のW2C用プリセットを適用します。夜間の窓照明を反映するには、**変換前にSunSetをセットアップ**してください。日中のワールドの影はゲームの **Shadow Detail（影の詳細）** を有効にしてください。SunSetなしでも基本の変換・配置は可能です。SunSetの `exhaust.nfsms` は任意の排気演出用で、ワールドには不要です。コンバーターからは適用しません。

**任意**

- 対応する **Need for Speed: Carbon PC版** のゲームフォルダー。Carbon固有のブレーカー・カメラ・環境エフェクトを復元するには、[Attribulator 2.0](https://github.com/NFSTools/Attribulator/releases/tag/v2.0.0) に [OGVI 1.4](https://nfsmods.xyz/mod/5290) を重ねて展開し、そのツールフォルダーも指定します。
- [Unlimiter](https://github.com/nlgxzef/NFSMWUnlimiter/releases) と [Extra Options](https://github.com/ExOptsTeam/NFSMWExOpts/releases) は導入済みなら検出しますが、基本構成では不要で、設定も変更しません。Event Loader、Event Editor、Save Virtualizerも前提ではありません。Save Virtualizerとセーブのサイドカーは同梱していません。

## 導入と元に戻す方法

1. ゲームとランチャーを終了し、セーブをバックアップします。別のワールドローダーが入っていない、独立したMost Wantedフォルダーを用意します。
2. **Releases → Assets** からWindows x64向けのコンバーターZIPをダウンロードし、すべて展開します。`_internal` を `W2C-to-MW-Converter.exe` の隣に置いたまま、EXEを起動します。
3. W2C原本、Most Wantedの導入先、品質を選びます。必要に応じてCarbonや描画の資源を指定し、導入先の検査を完了します。
4. 新しい出力フォルダーを選び、変換します。そのまま「自動配置」するか、後から「変換済みパックを配置」で適用してください。変換済みパックを手動コピーしただけでは反映されません。配置後は、その導入先の `speed.exe` で起動します。
   - 初回の変換時にセットアップを確認してください。固定版の上流ソースと、必要な場合は一時使用の.NET SDKを取得して照合し、原本を読み込む部品をPC内でビルドします。初回はインターネット接続が必要です。PythonやVisual Studioの導入は不要です。[ビルド手順（英語）](BUILD.md)
5. 元のワールドに戻すには、ゲームを終了し、導入先の `W2CMWPublic/W2CMW-Rollback.exe` で復元を確認します。セーブ、変換済みパック、SunSet、TexWizardは保持されます。

## 再配布について

**変換生成物の再配布は厳禁です。** 変換済みパックの全部または一部、取り出した資産、生成物入りの導入済みゲームフォルダーは、アップロード、ミラー、個人間の共有、MODパックやコレクションへの同梱をしないでください。このリリースで配布するのはコンバーターです。紹介の際は配布ページへのリンクを共有し、利用者がそれぞれの原本から変換してください。[W2C原本の配布条件](https://nfsmods.xyz/mod/3041) と第三者コンポーネントの条件も引き続き適用されます。

## 既知の問題・制約

- 一部のMW純正イベントでチェックポイントを正常に通過できない場合があります。ワールドやイベントの切り替え時には読み込みが長引く場合があります。
- CarbonとOGVIを指定しない場合、一部のCarbon固有ブレーカー、追跡用マーカー、専用カメラ、環境エフェクトは省略されます。小物や設備はMW資源での代替、外観のみの再現、または省略になる場合があります。詳細は変換完了時のレポートで確認できます。動く小物自身の落ち影には対応していません。
- X360の長距離試験で一時的なフレーム落ちが一度発生しました。同区間の追試では再現せず、原因は特定できていません。発光している窓の一瞬のチラつきも報告があり、解消は確認できていません。
- 武器MODを併用したカスタム8台ホットパースートで、過去にCTDを確認しています。この組み合わせは初版で未再検証です。警察・武器なしの8台スプリントでは全車の実ゴール通過を確認しましたが、計測中に純正リザルト画面への遷移は確認できませんでした。
- SunSetの導入が不完全だと、フリーロームへ入る際にCTDすることがあります。SunSetを使う場合は、配置前にTexWizardと描画資源の検査を通してください。同じワールドを置き換えるMODとの併用は競合する場合があります。変換後に車両追加などでゲームの属性データを編集した場合は、その導入先を指定して再変換してください。

不具合報告には、選んだ品質、Carbon／OGVIとSunSetの有無、併用MOD、発生地点や再現手順を添えてください。変換済みパック、取り出したゲーム資産、セーブデータの添付は不要です。

## クレジット

- **W2C to MW Converter**：Zakkey250（Nexus ModsではZakkey76）— 企画、統合、検証。
- **原本 [W2C（World to Carbon）](https://nfsmods.xyz/mod/3041)**：MaxHwoy、および rx、RaTT、379Felipe、r033、mzry、Crossy、avail、konigsegg、David、heyitsleo。各貢献者の詳細と原本のダウンロードはリンク先をご覧ください。
- **ゲームと原本資産**：Electronic Artsと、Need for Speed: Most Wanted、Carbon、Worldの各開発チーム。本作は非公式のファンプロジェクトです。
- **関連ツール**：ThirteenAG（[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)）、ARCHIE（[SunSet](https://nfsmods.xyz/mod/6659)、[OGVI](https://nfsmods.xyz/mod/5290)）、R-033（[TexWizard](https://github.com/R-033/texwizard)）、NFSTools（[Attribulator](https://github.com/NFSTools/Attribulator)、[VaultLib](https://github.com/NFSTools/VaultLib)、[NFS-ModTools](https://github.com/NFSTools/NFS-ModTools)）、KilLo445（[NFSPatcher](https://github.com/KilLo445/NFSPatcher)）、NTCore（[4GB Patch](https://ntcore.com/4gb-patch/)）。
- **ライブラリ・開発資料**：[NFSPluginSDK](https://github.com/berkayylmao/NFSPluginSDK)、[MinHook](https://github.com/TsudaKageyu/minhook)、[LZ4](https://github.com/lz4/lz4)、[Unicorn](https://github.com/unicorn-engine/unicorn)、[NFSMWのリバースエンジニアリング資料](https://github.com/s-b-repo/nfsmw-re)。配布物内の第三者ライブラリの通知もご覧ください。

## AI利用について

OpenAI Codex（GPT-6）を使い、コンバーター、ロールバッカー、ゲーム内ランタイムのコードを生成・修正し、UIの翻訳とこの紹介文の作成にも利用しました。初期の再設計検討にはFable 5.2の提案を参照しました。プロジェクトの要件決定と結果の確認は作者が行いました。コンバーターはローカルで動作し、AIサービスへ接続しません。
