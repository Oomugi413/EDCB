# 録画・予約に関係する Windows 独自機能の再調査

調査日: 2026-09-06

## 結論と読み方

確認した範囲では、**Linux の録画エンジンそのものが機能を欠いているケースと、Windows の EpgTimer にだけ便利な予約操作があるケースは、分ける必要がある**。

本書では、Windows 側に残る予約操作・録画補助機能 14 件と、Linux に移植済みの自動予約連動に残る保護判定の差分 1 件、計 **15 件**を、実装時の修正箇所で分類する。

- **A: サーバー側の修正が必要なもの（3 件）**。WebUI の変更も必要になるものを含む。サーバー側のスクリプトで対応する選択肢も含める。
- **B: Web UI の修正のみで解決するもの（12 件）**。既存の EpgTimerSrv の API を利用し、Legacy／E3 の画面・JavaScript・WebUI 付属の Lua／HTTP API を変更する。EpgTimerSrv 本体の改修は不要という分類であり、画面の HTML だけで完結するという意味ではない。

類似機能があるものは、未反映の部分だけを対象としている。

特に重要な訂正は、自動予約変更時の「録画中・開始直前の予約を削除しない」判定である。これは単なる Windows の警告表示ではない。現在の Linux 実装とは保護時間と開始マージンの扱いが異なり、条件によっては録画中の予約の削除に関係する。

ここでいう「Windows 独自」は、原則として **Windows の EpgTimer にだけ該当する操作・補助処理が実装されている**という意味である。多くの項目で、操作の結果を保存・実行する Linux 側の予約 API は存在する。Windows クライアントや独自スクリプトから同じ予約データを送ることまで不可能、という意味ではない。

## 調査対象・判定基準

- EDCB 作業ツリー: `527eaf1ced0d6a86c93531752cff2adec1917187`。Windows の WPF クライアント、共通コード、Linux サーバー、Git 履歴を照合した。
- 手元の新しい参照 `20260831` / `origin/20260831`: `e53f5bf35cf326fd2040595a05d53374f6c5d894`。この参照までの差分も確認した。対象サーバー・録画系ディレクトリの作業ツリーとの差分はバージョン情報で、以下の判定を覆す録画処理の変更は確認できなかった。
- 配置済み Legacy WebUI: [/var/local/edcb/HttpPublic/legacy](/var/local/edcb/HttpPublic/legacy)。リポジトリ側の [ini/HttpPublic/legacy](/home/oomugi413/git/EDCB/ini/HttpPublic/legacy) も参照した。
- 配置済み設定・関連スクリプト: [/usr/local/lib/edcb](/usr/local/lib/edcb)、[/var/local/edcb](/var/local/edcb)。主に INI、Lua、シェルスクリプトと、それを読み出す C++ / C# の実装を確認した。
- Material WebUI: [/home/oomugi413/git/EDCB_Material_WebUI](/home/oomugi413/git/EDCB_Material_WebUI)、`bf45c7395da25d19d138d7da03ca6d99ada14a67`。現在の `E3` / `api` と、配置済みの旧 [/var/local/edcb/HttpPublic/EMWUI](/var/local/edcb/HttpPublic/EMWUI) を確認した。

設定キーが実ファイルに書かれていないだけでは「未実装」と判定していない。既定値で働く機能、Linux 側にも読み取り・実行処理がある機能は除外した。また、番組表の色・列・強調表示・検索結果表示など、サーバーに送る予約内容も録画処理も変わらない機能は除外した。

以下の「WebUI にない」は、調査対象の画面と API に該当する操作が見つからなかったという判定である。設定ファイルや API を手作業で編集すれば同じ結果を作れる場合もある。Git のコミットは導入・拡張・現在の実装につながる変更を示し、初出を特定していないものはその旨を明記する。

外部リポジトリの更新取得や、稼働中の予約の追加・変更・削除、録画中断の再現試験は行っていない。配置済み実行ファイルとソースの完全な一致を検証したものでもなく、以下は手元の履歴・ソース・配置ファイルに基づく静的調査である。

## A. サーバー側の修正が必要なもの

| 項目 | 対象機能 | 主な修正箇所 | 主なコミット |
| --- | --- | --- | --- |
| A-1 | 自動予約変更時の削除・再作成で、録画中／開始直前の予約を保護する判定 | EpgTimerSrv。設定画面を設ける場合は WebUI も変更 | `6c5fd4de`, `81915066` |
| A-2 | 自動予約の評価順に反映される並べ替えの保存 | WebUI＋予約連動を起こさず順序を更新するサーバー側の処理 | `79033d2a` |
| A-3 | 録画準備時の HDD 起動用アクセス | サーバー側の常駐処理、または録画準備に連動するスクリプト | `ae750098` |

### A-1. 自動予約変更時の削除・再作成で、録画中／開始直前の予約を保護する判定

**修正箇所は EpgTimerSrv の連動処理**。開始マージンを加味した保護判定と、保護時間の設定への対応が必要になる。設定画面を設ける場合は WebUI も変更する。

#### 自動予約連動そのものは実装済み

Windows の連動オプションの主要な導入は `bc477fd1a9edcd57313d208c65e3eef28beeadb9`（2016-01-30）。Linux 側は `819150664021e67f9e48cd54198a04e06aa5b2c1`（2026-06-27、`linux_autodelete_reservaions`）でサーバーに処理が入り、現在の設定も次のとおりである。

| 設定 | 配置済みの値 | 判定 |
| --- | --- | --- |
| `SyncResAutoAddChange` | `1` | 自動予約変更時の予約連動を実装済み |
| `SyncResAutoAddDelete` | `1` | 自動予約削除時の予約連動を実装済み |
| `SyncResAutoAddChgNewRes` | `1` | 変更時に対象予約を削除・再作成する処理を実装済み |
| `SyncResAutoAddChgKeepRecTag` | `0` | タグ保持の処理は実装済みだが無効。削除・再作成しない場合に作用 |

根拠: [配置済み INI](/var/local/edcb/EpgTimerSrv.ini:51)、[サーバーの設定読み取り](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.cpp:137)、[サーバーの連動処理](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:1675)。

したがって、自動予約の変更・削除に予約を連動させる機能全体を「Linux 未反映」として一覧に残すのは誤りである。既存の Linux 移植提案書にある「Windows クライアントにだけ存在する」という背景説明は、現在の実装状況とは区別する必要がある。

#### 残る差分

Windows は `6c5fd4de0c1514360771f382d0f55d037f040906`（2016-02-01）で、**自動予約の変更に伴う削除・再作成**から録画中・開始直前の予約を保護する処理を追加している。

| 比較点 | Windows EpgTimer | 現在の Linux サーバー |
| --- | --- | --- |
| 保護時間 | 警告オプションが有効なら `CautionOnRecMarginMin`（既定 5 分）、無効なら 1 分 | 固定 60 秒 |
| 開始時刻の基準 | `OnTime()` が参照する、開始マージンを加味した録画開始時刻 | 予約に保存された番組開始時刻 `startTime` |
| 判定に使う設定 | 変更後の録画設定を複製した予約 | 変更前の予約データ |
| Linux での設定による一致 | — | `CautionOnRecChange` / `CautionOnRecMarginMin` を読み取ってこの判定へ適用する処理はない |

Windows は削除対象を作るときにこの値を使うので、`CautionOnRecMarginMin` は **この経路では警告表示だけの設定ではない**。

根拠: [Windows の削除対象の保護判定](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:527)、[マージン込みの時刻判定](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/CtrlCmdDefEx/ReserveDataEx.cs:47)、[Windows の既定値](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Common/SettingClass.cs:981)、[Linux の固定 60 秒・番組開始時刻による判定](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:1712)。

#### 実録画への影響があり得る条件例

同じ開始マージンを変更前後で維持する例として、番組開始が 12:00、開始マージンが 120 秒なら、録画開始は 11:58 である。

- 11:58:30 に自動予約を変更すると、Linux の比較は `12:00 > 11:59:30` となる。
- 有効な自動追加予約で、ほかの有効な自動予約による保護もない等、残りの削除条件を満たせば、Linux では削除対象に入る。
- Windows ではマージン込みの開始時刻 11:58 が既に過ぎているので、この削除・再作成の対象に入らない。

Linux の予約削除はチューナー側の予約削除まで進み、録画中なら停止コマンドを送る経路がある。したがって、この差は **単なる予約一覧の見え方ではなく、条件次第で録画中断につながる可能性がある**。

根拠: [予約削除からチューナーへの反映](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/ReserveManager.cpp:431)、[録画中の停止処理](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/TunerBankCtrl.cpp:125)。この例はソースからの推論であり、実際の録画を止める試験はしていない。現在の共通開始マージンは [15 秒](/var/local/edcb/EpgTimerSrv.ini:39) であり、例の 120 秒は差分を示すための仮定である。

また、ここで保護するのは「自動予約の**変更**に伴う削除・再作成」である。「自動予約ルール自体を**削除**したときにも Windows は常に録画中の予約を保護する」と一般化してはいけない。削除せずに残した予約についても、録画設定の変更まで全面的に禁止する仕組みではない。

### A-2. 自動予約の評価順に反映される、並べ替えの保存

**WebUI の並べ替え操作に加え、安全に順序だけを更新するサーバー側の処理が必要**。

Windows の自動予約一覧の並べ替え保存は、画面のソートだけではない。項目の `DataID` を並び順に合わせて付け替え、自動予約変更コマンドでサーバーへ送る。追加・変更・削除の前に未保存の並べ替えを保存する処理もある。

サーバーの EPG 自動予約は ID 順で処理され、既存予約や追加待ち予約との重複を確認する。このため、条件が重なるルールの順序は、**これから作る予約にどのルールの録画設定が採用されるか**に影響し得る。これは録画設定の「優先度」やチューナーの優先順位とは別の話である。

Legacy／Material の表示の並べ替えに、同等のサーバー側ルール ID の付け替え・保存は確認できなかった。ルール自体の保存形式や評価処理は Linux にもあり、欠けているのはこの並べ替え操作である。並べ替えだけで既存予約を常に希望の設定へ更新できる、という意味ではない。

- 関連コミット: `79033d2a058063ddb85eb67099fc7304611e69ba`（2016-04-29、追加・変更・削除前の未保存順序の保存。並べ替え機能自体はそれ以前から存在する）。
- Windows の根拠: [ID を付け替えてサーバーに保存](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/AutoAddListView.xaml.cs:39)、[操作前の自動保存](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:661)。
- 録画への影響の根拠: [サーバーの自動予約の走査順](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:811)、[重複確認と録画設定の採用](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:1825)。順序による結果の差はこの処理からの推論であり、実予約での比較試験はしていない。

現在の Linux の自動予約変更 API は、ルール変更時に `SyncChangeAutoAddReserveData()` を呼ぶ。そのため、WebUI から既存 API で ID を順番に付け替えるだけでは、並べ替えに伴って既存予約の変更・削除が誘発されるおそれがある。予約連動や途中状態での自動予約生成を起こさず、まとめて順序だけを更新するサーバー側の処理を用意する方針とし、WebUI のみの修正には分類しない。根拠: [現在の Lua 自動予約変更 API](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:4615)。

### A-3. 録画開始準備時に、録画先 HDD を起こすアクセスを発生させる

**サーバー側の常駐処理、または録画開始準備に連動するスクリプトで対応する**。WebUI を開いていなくても動く必要があるため、WebUI の修正だけでは解決しない。設定画面を設ける場合は WebUI も変更する。

Windows は「予約録画開始準備」の通知を受けて、対象の録画先に空の一時フォルダを作成・削除し、ディスクアクセスを発生させられる。録画開始時の HDD 起動待ちを事前に発生させるための補助機能であり、改善効果はディスクや環境に依存する。

対象キーは `WakeUpHdd`、`NoWakeUpHddMin`、`WakeUpHddOverlapNum`。保存先は `EpgTimerSrv.ini` だが、**読んで実行するのはサーバーではなく Windows の EpgTimer**である。同名のキーを Linux の INI に追記するだけでは動かない。

さらに `NWMode` では処理しないため、録画 PC 上のローカル EpgTimer を常駐させる必要がある。Windows の EpgTimerNW から Linux サーバーへ接続するだけでも動かない。調査対象の Linux 設定、サーバー実装、WebUI、配置済み録画関連スクリプトに同等の組み込み処理は確認できなかった。

- 導入コミット: `ae75009807a41c852fd6d349a45c618d5477efba`（2017-05-29）。
- 根拠: [設定の読み取り](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Common/SettingClass.cs:756)、[ローカルモード限定・対象フォルダ抽出・一時フォルダ操作](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Common/CommonManagerClass.cs:1589)、[録画開始準備通知からの呼び出し](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/MainWindow.xaml.cs:1378)。

## B. Web UI の修正のみで解決するもの

既存の予約・自動予約 API を利用する。ここには Legacy の Lua や E3 の HTTP API の追加・修正も含む。対象予約の照合、確認画面、途中失敗時の扱いなどは WebUI 側で実装する必要がある。

| 項目 | 未反映の操作・処理 | 録画・予約への影響 | 主なコミット |
| --- | --- | --- | --- | --- |
| B-1 | 終了時刻未定の番組を仮の長さで EPG 予約 | WebUI では追加できない時点で予約を作れる | `8d49203f`, `a83a62b3` |
| B-2 | プログラム予約を EPG 予約に変換 | 番組との対応付け、追従・ぴったり録画の利用条件が変わる | `e16ba539` |
| B-3 | 個別予約を自動予約連動から外す・再生成で戻す | 後の自動予約変更・削除の対象が変わる | `bc477fd1`, `6b669dd3` |
| B-4 | 既存予約を自動予約の録画設定に一度だけ合わせる | 手動で追加した一致予約も含めて設定を変更できる | `6e813389` |
| B-5 | 自動予約を一致予約ごと明示的に削除 | 通常の連動削除より広い対象を削除できる | `fcb2ef24`, `f0925337` |
| B-6 | 番組表ごとの既定の録画設定 | 簡易予約などで送る録画設定が変わる | `504e93e8` |
| B-7 | キーワード自動予約作成時に元予約の録画設定・番組ジャンルを引き継ぐ | 作成するルールの対象番組・録画設定が変わる | `80a8478e`, `51b4dcc8` |
| B-8 | 録画タグだけを保持してプリセットを適用 | タグを参照する後処理への入力が保持される | `baed76e5` |
| B-9 | 個別予約・プログラム自動予約のコピー追加、複数項目のコピー | 新しい予約・自動予約を追加できる | `d2cf79d1` |
| B-10 | 変更・削除した予約／自動予約の履歴から再追加 | 過去の内容で予約・自動予約を再作成できる | `bb189af9` |
| B-11 | 複数選択した予約・自動予約の録画設定の一括変更 | 複数の予約・ルールに同じ変更を適用できる | `2b1c4015` |
| B-12 | iEPG ファイルからの予約追加 | ファイルを読み取って予約を作成できる | `27a579db`, `4fdea5f5` |

### B-1. 終了時刻未定の番組を、仮の長さで EPG 予約する

Windows は、開始時刻が判明している将来の番組なら、終了時刻が未定でも予約を追加できる。現在の処理は番組長を仮に **300 秒**として、番組 ID を持つ EPG 予約を作る。放送中の番組では、この仮の番組長を用いた終了判定などにも制約されるため、終了時刻未定ならいつでも追加できるという意味ではない。

Legacy の追加処理と Material の `EventInfo()` は、開始時刻に加えて `durationSecond` の存在を要求する。このため、同じ終了時刻未定の番組を EPG 予約として追加する操作が通らない。手動で時間を指定するプログラム予約とは異なる。

Linux サーバーの番組追従機能が欠けているという話ではない。また、これは **手動の EPG 予約追加**の差であり、サーバーのキーワード自動予約も終了時刻未定の番組を拾う、という意味ではない。

- 関連コミット: `8d49203fde012860e0c00872e882b2677f31174c`（2015-03-15、既存の仮 10 分予約処理を共通化。機能の初出ではない）、`a83a62b385a64ee04e210aef03833d87283cfca7`（2016-08-18、仮 5 分の `PgDurationSecond` に統一）。
- Windows の根拠: [追加可能判定](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:205)、[仮の番組長](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/CtrlCmdDefEx/EpgEventInfoEx.cs:13)、[予約データへの変換](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/CtrlCmdDefEx/CtrlCmdDefEx.cs:140)。
- 比較根拠: [Legacy の追加条件](/var/local/edcb/HttpPublic/legacy/epginfo.html:39)、[Material の追加条件](/home/oomugi413/git/EDCB_Material_WebUI/HttpPublic/api/SetReserve:11)、[終了時刻未定なら Lua に番組長を渡さない処理](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:4815)、[サーバーの自動予約側の条件](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:1815)。

### B-2. プログラム予約を EPG 予約に変換する

Windows は、時刻・サービスを指定したプログラム予約から、時間帯が重なる対応番組を探し、番組 ID・開始時刻・番組長などを入れ直して EPG 予約へ変換できる。番組追従やぴったり録画を利用する前提が変わるため、表示だけの機能ではない。

Legacy は **EPG 予約からプログラム予約への変換**を提供するが、逆変換は提供せず、画面にも「元に戻せません」と明記している。Material の予約変更 API もプログラム予約化は行うが、既存のプログラム予約を番組に結び直す処理はない。

- 導入コミット: `e16ba539c9442e361ea8a8a384142d5e814e6035`（2015-03-17）。
- Windows の根拠: [予約モード変換](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:329)、[画面の選択条件・説明](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/ChgReserveWindow.xaml:40)。
- 比較根拠: [Legacy の説明](/var/local/edcb/HttpPublic/legacy/reserveinfo.html:192)、[Material のプログラム予約化](/home/oomugi413/git/EDCB_Material_WebUI/HttpPublic/api/SetReserve:23)、[Material の変更処理](/home/oomugi413/git/EDCB_Material_WebUI/HttpPublic/api/SetReserve:98)。

### B-3. 個別予約を自動予約連動から外す／再生成で戻す

Windows の「自動予約登録を解除する」は、ルールそのものを消すのではなく、その予約だけを個別予約扱いに変える。予約コメントの末尾に `$` を付け、後の「自動予約登録の変更・削除に合わせて予約を変更・削除する」対象から外す。

Linux 側もこの印を認識して連動対象から除外する。欠けているのは **WebUI から印を付ける操作**であり、Linux に解除状態の解釈がないわけではない。通常の Legacy／Material の予約編集には対応する選択肢がない。

Windows の右クリックメニューには戻す操作もある。ただし既存予約の印を単純に消すのではなく、予約を削除して指定した自動予約を再評価させる処理であり、元の予約 ID を保つ操作ではない。

- 関連コミット: `bc477fd1a9edcd57313d208c65e3eef28beeadb9`（2016-01-30、予約モード・連動処理）、`6b669dd337bfaec795d74d0d4748b163f191f0b9`（2016-05-15、解除チェックボックスを追加）。
- Windows の根拠: [解除の説明](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/ChgReserveWindow.xaml:43)、[解除状態の表現](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/CtrlCmdDefEx/ReserveDataEx.cs:37)、[再生成で戻す処理](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:351)。
- Linux の根拠: [自動追加予約の判定](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:117)、[連動変更での除外](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:1681)。

### B-4. 既存予約の録画設定を、自動予約の設定に一度だけ合わせる

Windows の「予約の録画設定を自動登録の録画設定に合わせる」は、自動予約条件を編集せずに実行できる。該当する既存予約を集め、録画モード、マージン、保存先、チューナー指定、後処理などをルールの設定に合わせる。予約の無効状態は保持し、プログラム自動予約では予約名も追従させる。

この操作は `SyncAll=true` で、検索・対応付け上そのルールに一致する個別予約も対象になる。Linux に移植済みの `SyncResAutoAddChange` は「ルールを変更したとき、自動追加由来の予約に連動する」機能なので同一ではない。Legacy／Material に一度だけ全一致予約へ適用する専用操作はない。

- 導入コミット: `6e81338948941b6d90a0e059c727c51166e6f904`（2015-09-13）。
- 根拠: [コマンドの呼び出し](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/CmdExeAutoAdd.cs:49)、[全一致予約への適用と設定の複製](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:482)、[Linux の通常連動の対象制限](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:1681)。

### B-5. 自動予約を、一致する予約ごと明示的に削除する

Windows の「削除（予約ごと削除）」は、選択した自動予約と、そのルールに対応する既存予約をまとめて削除する専用コマンドである。現在の `SyncAll=true` 経路では、個別予約や、別の有効な自動予約にも一致する予約を、通常の連動削除のようには除外しない。

Linux の `SyncResAutoAddDelete` は既に実装済みだが、「自動追加由来で、削除後にほかの有効なルールが残らない予約」だけを削除する。したがって、ここで未反映としているのは通常の連動削除ではなく、**より広い対象を削除する明示的な一回操作**である。Legacy／Material の削除 API には、この対象拡張を選ぶ操作がない。

なお、別の有効なルールを残した場合、そのルールによる後の予約再生成を永続的に禁止する機能ではない。

- 導入コミット: `fcb2ef24d37064960d52da2620d1dbca25255e72`（2014-04-21、EPG 自動予約）、`f0925337a969e1b444bfb0850ce721eb787f4a7e`（2015-03-29、プログラム自動予約）。
- Windows の根拠: [明示的な予約ごと削除](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/CmdExeAutoAdd.cs:44)、[SyncAll の対象選択](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:555)。
- 比較根拠: [Linux の通常連動削除の対象](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:1784)、[Material のルール削除](/home/oomugi413/git/EDCB_Material_WebUI/HttpPublic/api/SetAutoAdd:19)。

### B-6. 番組表ごとに、簡易予約などで使う録画設定を持たせる

Windows は各番組表の設定に `RecSetting` を保持し、その番組表から簡易予約などを行う際の既定値に使う。たとえば番組表 A は保存先 A、番組表 B は保存先 B、といった使い分けを予約追加時に自動で反映できる。未設定なら従来どおりデフォルトの設定になる。

Material のカスタム番組表・検索プリセット、一般の録画プリセット選択は類似する部品だが、**番組表ごとの録画設定を予約追加に自動で結び付ける機能**は確認できなかった。現在の E3 の新規予約はデフォルトの録画プリセットを初期値にする。

- 導入コミット: `504e93e810177ba34f7f491f423657c8f5c5ed9f`（2019-08-04）。
- Windows の根拠: [番組表の録画設定フィールド](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/DefineClass/CustomEpgTabInfo.cs:36)、[予約コマンドへの引き渡し](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/EpgView/EpgViewBase.cs:219)。
- 比較根拠: [E3 の新規追加の初期設定](/home/oomugi413/git/EDCB_Material_WebUI/HttpPublic/E3/js/app.js:2522)、[番組詳細からの予約の初期設定](/home/oomugi413/git/EDCB_Material_WebUI/HttpPublic/E3/js/app.js:2564)。

### B-7. キーワード自動予約を作る際に、録画設定・ジャンルを引き継ぐ

「番組名でキーワード予約作成」そのものは Material にもあるため、Windows 独自機能には数えない。差分は、**元予約の録画設定を引き継ぎ、番組情報があればジャンル条件も自動設定できる部分**である。

Windows は録画設定を持つ元データからプリセット／個別設定を引き継ぐ。また `SetJunreToAutoAdd` と `SetJunreContentToAutoAdd` により、取得できる番組ジャンルを検索条件に設定できる。これによって新しいルールが録画する番組の範囲や保存先・マージンなどが変わる。

E3 の `openNewEntry(e)` はタイトルとサービスを引き継ぐ一方、録画設定はプリセット 0 で初期化し、この経路で番組のジャンルは取り込まない。ジャンルや録画設定を利用者が手入力・選択する機能は存在する。

- 導入コミット: `80a8478e606c9409f5a0e36ed70d969578682db9`（2016-02-09、録画設定の引き継ぎ）、`51b4dcc8f3dea0a8dcf5e449c539c9dcc61e68f0`（2017-05-11、ジャンル引き継ぎ）。
- Windows の根拠: [元予約・番組の受け渡し](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/CmdExeReserve.cs:213)、[録画設定の引き継ぎ](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:835)、[ジャンル条件の設定](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:860)。
- 比較根拠: [E3 のタイトル・サービスのみの引き継ぎ](/home/oomugi413/git/EDCB_Material_WebUI/HttpPublic/E3/js/app.js:2522)。録画済み情報など、元データに録画設定がない場合まで元の録画設定を復元する機能ではない。

### B-8. 録画タグだけを保持して、別の録画プリセットを適用する

Windows の「録画タグを除く」／`SetWithoutRecTag` は、別の録画プリセットを適用するときに、既存の録画タグを残す。複数項目へ録画設定を適用する際にも、各項目が元から持つタグを保持できる。

Material にも録画タグとプリセットの機能はあるが、この「タグだけは上書きしない」指定はない。E3 のプリセット適用は `recSetting` 全体を複製する。

録画タグ自体は Linux でも対応しており、後処理へ `BatFileTag` として渡される。したがって影響は、タグを参照する BAT／Lua／シェル等の振り分け・後処理である。**録画時間やチューナー割り当てを直接変える機能ではなく、タグを使わない環境では録画結果への影響もない**。タグが標準で録画ファイル名マクロになる、という意味でもない。

- 導入コミット: `baed76e5cf4ba85e1c08f61d77e0d5a19b8a5ff6`（2020-04-16）。
- Windows の根拠: [プリセット変更時のタグ保持](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/UserCtrlView/RecSettingView.xaml.cs:269)、[一括適用時のタグ保持](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:301)。
- 比較根拠: [E3 のプリセット適用](/home/oomugi413/git/EDCB_Material_WebUI/HttpPublic/E3/js/app.js:2395)、[Material のタグ受け取り](/home/oomugi413/git/EDCB_Material_WebUI/HttpPublic/api/util.lua:1618)、[Linux の後処理へのタグ受け渡し](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/ReserveManager.cpp:1400)。

### B-9. 個別予約・プログラム自動予約のコピー追加／複数項目のコピー

Windows の「コピーを追加」は、選択した予約・自動予約の内容を使い、新しい項目として追加する。予約はコメントを空にして個別予約として追加する。明示的に重複する予約を作る用途も含む。

ただし、**EPG 自動予約 1 件の複製に相当する操作は Legacy に既にある**。既存条件の詳細画面から「追加」を押すと、新しいルールとして登録される。これを Windows 独自とは数えない。

未反映として残すのは、個別予約やプログラム自動予約のコピー追加、および選択した複数項目をまとめてコピーする操作である。Material にも該当する複製操作は確認できなかった。設定を読み取って新規フォームに手入力することとは区別する。

- 導入コミット: `d2cf79d15a2597648efb1e0108f452000fd3ae89`（2017-08-12）。
- Windows の根拠: [予約の複製](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/CmdExeReserve.cs:165)、[自動予約の複製](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/CmdExeAutoAdd.cs:32)。
- 除外範囲の根拠: [Legacy の既存条件から新規追加する処理](/var/local/edcb/HttpPublic/legacy/autoaddepginfo.html:114)、[「追加」と「変更」の両ボタン](/var/local/edcb/HttpPublic/legacy/autoaddepginfo.html:281)。

### B-10. 変更・削除した予約／自動予約を、履歴から再追加する

Windows の「アイテムの復元」は、変更前・削除前の予約や自動予約をクライアント内の操作履歴に保持し、その内容を新規項目として再追加する。現在の履歴上限は 16 操作分で、永続的なサーバー側のごみ箱ではない。

これは **Undo ではない**。変更前の項目を復元すると、変更後の項目は残ったまま重複追加になる場合がある。録画済み情報や削除した TS ファイルの復元も対象外である。

Legacy／Material に、変更・削除済みの予約データをこのような履歴から再追加する処理はない。Material の画面状態・キャッシュ・入力欄の復元とは異なる。実際に予約追加 API を呼ぶため、単なる表示機能ではない。

- 導入コミット: `bb189af998b5a99c0087d84768e6a23cee3f8e2b`（2018-10-30）。
- 根拠: [メモリー内の操作履歴](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/CmdHistory.cs:8)、[復元コマンド](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/CmdExe.cs:315)、[実際の予約・自動予約の再追加](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:733)。

### B-11. 複数選択した予約・自動予約の録画設定を一括変更する

Windows は、複数の予約や自動予約を選択し、「まとめて録画設定を変更」から同じ録画設定を適用できる。任意の予約を対象にでき、特定の自動予約に対応する予約だけを変更する B-4 とは異なる。

Legacy／Material には個別の録画設定変更やプリセット編集はあるが、選択した複数予約／ルールに一つの録画設定を適用して保存する同等の操作は確認できなかった。プリセットそのものの編集と、既に登録済みの予約データへの適用は別の処理である。

- 関連コミット: `2b1c401583e5fea7dd52ba4d8719c11744753eb2`（2015-05-30、現行につながる `ChgBulkRecSet` コマンド構成を確認できる変更。すべての一括編集機能の初出を意味しない）。
- 根拠: [複数予約の録画設定変更](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/CmdExeReserve.cs:158)、[複数自動予約の録画設定変更](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/CmdExeAutoAdd.cs:27)、[共通の一括適用](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:283)。

### B-12. iEPG ファイルを読み込んで予約を追加する

Windows はコマンドライン引数やドラッグ＆ドロップで `.tvpid` / `.tvpio` / `.tvpi` を受け取り、サービス・日時などを解釈して予約を追加する。古い形式では放送局名との対応設定も利用する。

Legacy／Material に対応するファイルインポート処理・設定は確認できなかった。Linux の予約追加 API はあるが、iEPG ファイルを解釈する Windows クライアントの処理はそのまま移植されていない。

- 関連コミット: `27a579dbfa6ea6fb1db78a58156d1b2aa1e01744`（2013-02-12、取り込み済みの旧実装を確認できるコミット。真の初出ではない）、`4fdea5f588ee56a7732be996831f1eea2dc69dd4`（2017-10-05、iEPG 予約追加機能の整理）。
- 根拠: [対応拡張子・読み込み・予約送信](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/MainWindow.xaml.cs:897)。

## C. 設定はあるが Linux の実動作が未実装であるため、別扱いとしたもの

### 録画後のスタンバイ・休止・シャットダウン、および次回予約に向けた復帰

これは録画運用上の明確な Windows / Linux 差分である。ただし `RecEndMode`、`WakeTime`、`Reboot` などの設定や WebUI の録画後動作の選択肢が存在するため、依頼の「WebUI／設定ファイルのいずれにも反映されていない」という条件には合わず、A・B の計 15 件には含めていない。

UNIX ではこれらの電源制御と復帰処理が未実装であることを Readme が明記し、実装も `Shutdown is not supported` のログ出力になる。Windows の復帰タイマーの関連コミットには `c8f73c259966b0ff5a03a3f5fc6ac362c124d4c5`、`2e7a3acb406d5b50451ca11fe670bb1b25030362`（ともに 2014-05-01）がある。

根拠: [UNIX での未実装の明記](/home/oomugi413/git/EDCB/Document/Readme_Mod.txt:435)、[非 Windows の電源制御経路](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:886)、[Windows の復帰タイマー](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:1430)。Linux 側で別途 OS のタイマー・サービス等を組んだ場合の動作は、この組み込み機能の有無とは別である。

## D. Windows 独自として数えなかった主な機能

| 機能・候補 | 除外理由 |
| --- | --- |
| 通常の自動予約変更・削除への予約連動 | Linux サーバーと配置済み INI に反映済み。A-1 の残存差分だけを記載 |
| 通常の番組追従、ぴったり録画、録画マージン、保存先・チューナー指定 | Linux の共通予約・録画処理にも実装がある。B-1 / B-2 は予約を作成・変換する操作の差 |
| 録画タグそのもの、後処理へタグを渡す機能 | Linux／Material にも対応がある。B-8 はプリセット適用時に保持する指定だけ |
| 番組名からのキーワード自動予約作成、検索条件・ジャンル・プリセットの一般的な編集 | Material にも類似・同等機能がある。B-6 / B-7 は自動的な引き継ぎ・結び付けの差だけ |
| EPG 自動予約 1 件の条件・録画設定を使った新規追加 | Legacy の既存ルール詳細の「追加」で可能。B-9 からこの範囲を除外 |
| 代替チューナーへの再試行、固定チューナー予約の分離、予約コメントの自動追加 | `RetryOtherTuners`、`SeparateFixedTuners`、`CommentAutoAdd` の Linux 実装・配置済み設定がある |
| 録画済み判定の件数・正規表現、TS 拡張子、予約削除時の録画済み情報への登録 | `RecInfo2Max`、`RecInfo2RegExp`、`TSExt`、`DelReserveMode` などで Linux 側にも反映済み |
| 自動予約との対応表示、見失った予約の強調表示、番組表の表示形式・色・列 | それ自体では予約データや録画処理を変えない |

以上から、「録画・予約に影響する Windows 側の未反映機能はもうない」「残りはすべて表示だけ」とは結論できない。一方、Windows クライアントの予約支援操作の欠落を、そのまま Linux 録画エンジンの未対応と表現するのも正確ではない。
