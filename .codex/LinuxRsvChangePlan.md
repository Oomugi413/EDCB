# A-1 修正計画：Linux 自動予約連動の削除保護判定

作成日: 2026-09-06

実装担当: GPT-5.6 Luna

対象: [windows_feature.md の A-1](/home/oomugi413/git/EDCB/.codex/windows_feature.md:42)

## 1. Luna が最初に確認する実装範囲

本書は、自動予約の変更に伴う「既存予約を削除して再作成する」経路の保護判定を、Windows EpgTimer の仕様に合わせるための実装指示である。計画作成時点ではコード・設定の変更やテスト実行を行っていなかった。以下の実施結果は、計画に基づく A-1 のソース修正後に追記したものである。

後続の実装では、次の条件を守ること。

- WebUI は変更しない。Legacy、E3、EMWUI、その JavaScript、Lua、HTTP API、Material WebUI リポジトリへの変更は対象外。
- 新しい設定はサーバーの INI 読み取りで扱う。Windows の確認ダイアログを Linux に移植したり、確認待ちの通信仕様を追加したりしない。
- A-2 の並べ替え、A-3 の HDD アクセス、B の WebUI 機能は実装しない。
- 通常の予約変更・削除、自動予約ルールそのものの削除、チューナーの開始・停止処理に新たな保護条件を広げない。
- 本番サーバーの停止・起動、インストール、実際の予約操作、配置済み INI の書き換えは、この計画に基づくソース実装・検証とは別の作業とする。検証には独立したテストデータを使う。
- 前回の実装者や過去の説明を根拠に完了判定しない。「Windows の条件 → Linux の対応箇所 → テスト結果」の対応を本書の要件ごとに残す。

## 2. 正とするソースと今回の差分

調査時の EDCB HEAD は `527eaf1ced0d6a86c93531752cff2adec1917187`。実装開始時に HEAD と作業ツリーを確認し、行番号が変わっていれば以下の関数名・プロパティ名で探し直す。

| 根拠 | 内容 |
| --- | --- |
| `bc477fd1a9edcd57313d208c65e3eef28beeadb9` | Windows の自動予約連動処理 |
| `6c5fd4de0c1514360771f382d0f55d037f040906` | Windows の連動変更時の削除保護。コミット差分だけでなく現在の呼び先も読むこと |
| `819150664021e67f9e48cd54198a04e06aa5b2c1` | 現在の Linux 自動予約連動の移植 |
| [MenuUtil.cs: AutoAddSyncChangeList](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:486) | 変更後設定の複製、無効状態の保持、削除候補の抽出 |
| [MenuUtil.cs: 削除保護の式](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Menu/MenuUtil.cs:527) | `CautionOnRecChange ? CautionOnRecMarginMin : 1` と `OnTime(...) < 0` |
| [ReserveDataEx.cs](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/CtrlCmdDefEx/ReserveDataEx.cs:37) | `IsEnabled`、`OnTime`、`StartTimeActual`、`StartMarginResActual` |
| [RecSettingDataEx.cs](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/CtrlCmdDefEx/RecSettingDataEx.cs:68) | 予約固有マージンと既定マージンの選択 |
| [CtrlCmdDefEx.cs: onTime](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/CtrlCmdDefEx/CtrlCmdDefEx.cs:62) | 開始・終了境界の比較順と符号 |
| [SettingClass.cs: 既定値](/home/oomugi413/git/EDCB/EpgTimer/EpgTimer/Common/SettingClass.cs:981) | 警告設定有効、保護時間 5 分 |
| [EpgTimerSrvMain.cpp: SyncChangeAutoAddReserveData](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:1614) | Linux 側の修正対象 |

現状の Linux は、変更前予約の番組開始時刻を「現在時刻＋固定 60 秒」と比較している。Windows は、変更後録画設定を持つ予約について、開始マージンを加味した録画開始時刻を、設定可能な保護期限と比較している。この三点を一緒に移植する。

1. 保護時間を設定から求める。
2. 変更後の録画設定を使う。
3. マージンの既定値選択と負値補正を含めた時刻を使う。

固定 60 秒を 300 秒へ置き換えるだけ、または `startTime` から固有マージンを引くだけでは未完了である。

## 3. 確定する設定仕様

Linux サーバーの `EpgTimerSrv.ini` の `[SET]` に次を追加して読めるようにする。Windows クライアントの XML 設定をサーバーが直接読む設計にはしない。

| INI キー | 新規メンバー案 | 既定値 | Linux での意味 |
| --- | --- | --- | --- |
| `CautionOnRecChange` | `bool cautionOnRecChange` | `1` | 保護時間に次の設定値を使うかどうか。Linux で警告画面を出す指定ではない |
| `CautionOnRecMarginMin` | `int cautionOnRecMarginMin` | `5` | 上記が有効な場合の保護時間。単位は分 |

```ini
[SET]
CautionOnRecChange=1
CautionOnRecMarginMin=5
```

両キー未設定なら Windows の既定値に合わせて 5 分保護とする。これは現在の Linux の固定 1 分保護からの意図した変更であり、リリース説明にも記載する。既存の `SyncResAutoAddChange` 等の既定値は変えない。

`CautionOnRecChange=0` の場合も、保護そのものを無効にするわけではなく **1 分保護**にする。`CautionOnRecMarginMin=0` は有効な値とし、有効時は現在時刻を境界にする。

入力値の扱いも実装前に固定する。分数は `0..INT_MAX` の整数を受け付け、負数・整数以外・範囲外・空欄は既定値 5 に戻す。真偽値は範囲内の整数 0 を false、それ以外を true とし、不正値・未設定は既定値 true に戻す。この不正値処理は新しいサーバー設定の仕様であり、Windows の不正な XML 値まで再現するものではない。

[Linux の GetPrivateProfileInt](/home/oomugi413/git/EDCB/Common/PathUtil.cpp:748) は数値の前方部分を読み、範囲検証前に `int` へキャストする。上記仕様を満たすには、設定文字列を取得して変換の成否・末尾・範囲を確認するローカルな読み取り処理を用いる。共通の INI パーサー全体を変更しない。前後の空白は許容する。

設定の追加・読み取り・参照は Linux 側で整合させる。共通ファイルの Windows ビルドを壊さず、Windows サーバーの既存動作を今回の変更で変えないよう、新規設定と判定の適用を `#ifndef _WIN32` 等で明確に限定する。WPF クライアントの既定値や動作は変更しない。

起動時だけでなく、既存の `ReloadSetting()` でも反映する。[現在の再読込処理](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:1325) は設定を読み直して `this->setting` を置き換える。新しいメンバーを必ず初期化し、再読込後に古い値が残らないようにする。SIGHUP は再読込手段として使わない。この実装の UNIX では終了シグナルとして扱われる。

## 4. 判定式の移植仕様

### 4.1 使用する予約とマージン

削除候補を走査している `chgMap` の **`itr->second`** を判定対象 `r` とする。これは自動予約の変更後の録画設定を複製し、既存予約の無効状態を保持した予約である。

`reserveMap` の変更前予約を参照する既存の対応付け処理は保持するが、時刻・録画有効状態の判定を変更前設定へ戻さない。無効判定には `REC_SETTING_DATA::IsNoRec()` を使う。単純な `recMode == 5` への置き換えは不可。

| 値 | 求め方 |
| --- | --- |
| `S` | `ConvertI64Time(r.startTime)`。サーバー既存の時刻単位を使う |
| `D` | `r.durationSecond` を符号付き 64 bit に変換した秒数 |
| `rawMargin` | `r.recSetting.useMargineFlag != 0` なら `r.recSetting.startMargine`、それ以外はサーバー設定の `startMargin` |
| `effectiveMargin` | `max(-D, rawMargin)`。両項を符号付き 64 bit にそろえる |
| `actualStart` | `S - effectiveMargin * I64_1SEC` |
| `protectMinutes` | `cautionOnRecChange ? cautionOnRecMarginMin : 1` |
| `deadline` | 一度だけ取得した `now + int64(protectMinutes) * 60 * I64_1SEC` |

固有マージンのフラグ・メンバーは `useMargineFlag` / `startMargine`、共通設定は `StartMargin` / `startMargin` である。名前が似ていても同じ入力ではない。フラグが 0 の場合、固有マージン欄に残った数値は無視する。

開始マージンが正なら録画開始は早まり、負なら遅くなる。負のマージンは番組長の負値で下限補正する。例: 番組長 60 秒、開始マージン -120 秒なら、実効値は -60 秒である。

### 4.2 Windows の OnTime と等価な比較

削除可能な時刻条件は **`actualStart > deadline`** とする。等しい場合は保護する。`>=` にしない。

Windows の `OnTime()` はマージン込みの開始時刻と、0 以上に補正した長さを使い、終了済みなら 1、開始済みなら 0、開始前なら -1 を返す。通常の有効な予約日時では、長さが非負なので `OnTime(deadline) < 0` は `deadline < actualStart` と等価である。今回必要なのは開始前かどうかであり、終了マージンによって削除対象を変える処理を足さない。長さ 0 の境界もテストする。

[CReserveManager::CalcEntireReserveTime](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/ReserveManager.cpp:856) にも開始マージンの補正があるが、この関数は録画全体の時間算出と別の設定保持・ロックに関わる。呼べそうだからという理由で公開範囲を広げたり、設定取得元を混在させたりしない。必要な開始判定だけを、副作用のない小さな処理として実装する。

時刻は [TimeUtil](/home/oomugi413/git/EDCB/Common/TimeUtil.h:4) の `LONGLONG` / `I64_1SEC` / `GetNowI64Time()` / `ConvertI64Time()` に統一する。Unix 秒との混在、JST の二重加算、32 bit の中間乗算を避ける。`-r.durationSecond` を符号なし型のまま計算してはいけない。

### 4.3 既存処理への組み込み

1. `SyncChangeAutoAddReserveData()` の既存 `settingLock` 内で、連動フラグに加え、新しい二設定と共通 `startMargin` をローカル変数へ取得する。同一呼び出しでは一組の設定値を使う。
2. 既存どおり変更前ルールから対象予約を列挙し、`chgMap` に変更後設定を複製する。無効状態保持、プログラム予約名の変更、タグ保持、固定チューナー条件、ほかの有効ルールの扱いを維持する。
3. 既存の `syncChgNewRes` 分岐で `now` と `deadline` を一度だけ決める。予約ごとに現在時刻を取り直さない。
4. 現在の固定 60 秒・変更前 `startTime` の条件を、新しい「変更後設定で有効」かつ「`actualStart > deadline`」の条件へ置き換える。
5. `HasEnabledAutoAddAfterRemove(...) == false` など、時刻以外の削除条件を維持する。`SyncResAutoAddChange=0` の早期終了、`SyncResAutoAddChgNewRes=0` の削除なし経路も維持する。
6. 削除可能な予約だけを `delReserveList` に入れ、その予約だけを `chgMap` から消す。
7. **保護された予約は `chgMap` に残す**。既存の `PreChgReserveData()` → `ChgReserveData()` により録画設定の変更を受ける。保護を「変更も取り消す」と解釈しない。
8. その後のルール保存・新条件による予約生成・通知は既存の呼び出し元に任せる。ここに別の再生成処理を追加しない。

Windows と Linux には、無効な自動予約に関する除外と削除候補作成の順序にも違いがある。今回の A-1 では現行 Linux のその順序を維持し、時刻保護修正に混ぜて全面的な同期アルゴリズムの書き換えをしない。

### 4.4 忠実移植と、別の安全仕様を混同しない

Windows と同じく変更後設定で判定するため、変更で開始マージンを小さくすると、変更前には録画開始済みでも、変更後の計算では削除可能になる場合がある。たとえば現在 11:54:30、番組開始 12:00、変更前マージン 600 秒、変更後 0 秒、保護 5 分なら、変更後の開始 12:00 は期限 11:59:30 より先なので削除可能になる。

今回の受け入れ条件は Windows の当該判定との一致である。変更前・変更後の両方を見て必ず録画中を保護する、といった追加仕様を黙って入れない。この例はテストし、完了報告に限界として記載する。録画中断をあらゆる条件で防ぐ修正と表現しない。

また、Windows の `CheckReserveOnRec()` による確認ダイアログは別処理である。Linux では対話を追加せず、保護した予約にも既存どおり設定変更を行う。一般の削除 API や `SyncDeleteAutoAddReserveData()` へこの期限判定を足さない。

## 5. 修正ファイルと役割

| ファイル | 必要な修正 |
| --- | --- |
| [EpgTimerSrvSetting.h](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.h:47) | Linux 用の二設定を追加。初期化・条件コンパイルを読み取り側と一致させる |
| [EpgTimerSrvSetting.cpp](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.cpp:129) | `[SET]` の二キーを読み、既定値・不正値の方針を実装 |
| [EpgTimerSrvMain.cpp](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:1614) | 設定のスナップショット取得、変更後予約の実効開始時刻、削除期限の比較 |
| テスト用ファイル（新設可） | 以下の表の条件、実際の設定読込、共通連動処理への接続を再現可能に検証 |
| Markdown 文書 | Linux 用 INI 二キー、既定値変更、確認結果、未実行の検証を記録 |

テストのため必要なら、副作用のない判定部分を同ディレクトリの小さなヘッダーへ分離してよい。ただし本体とテストが同じ実装を呼ぶ構成にする。テスト側だけに正しい式を書いて、本体の誤りを検知できない構成にしない。予約管理全体や共通時刻処理のリファクタリングは不要。

## 6. 経路の移植漏れを防ぐ確認表

修正は共通の `SyncChangeAutoAddReserveData()` に置く。以下の入口は現在すべてそこへ到達する。入口ごとに期限判定を複製しない。

| 入口 | 予約種別 | 現在の呼び出し位置 |
| --- | --- | --- |
| `CMD2_EPG_SRV_CHG_AUTO_ADD` | EPG 自動予約 | [2275 行付近](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:2275) |
| `CMD2_EPG_SRV_CHG_MANU_ADD` | プログラム自動予約 | [2350 行付近](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:2350) |
| `CMD2_EPG_SRV_CHG_AUTO_ADD2` | EPG 自動予約 | [2924 行付近](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:2924) |
| `CMD2_EPG_SRV_CHG_MANU_ADD2` | プログラム自動予約 | [2993 行付近](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:2993) |
| `CMD_EPG_SRV_CHG_AUTO_ADD` | 旧形式 EPG 自動予約 | [3151 行付近](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:3151) |
| `LuaAddOrChgAutoAdd` の変更分岐 | EPG 自動予約 | [4634 行付近](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:4634) |
| `LuaAddOrChgManuAdd` の変更分岐 | プログラム自動予約 | [4681 行付近](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:4681) |

既存 Legacy／E3 は Lua API を使うため、WebUI の変更なしでこの共通処理に到達する。Lua の二入口を落として通信コマンドだけ直した場合は未完了である。

通常の予約変更、ルール削除、ルール新規追加の入口には新しい処理を追加しない。設定の再読込は通信側の既存経路と [LuaReloadSetting](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp:4063) の双方を確認する。

## 7. 必須の検証仕様

### 7.1 時刻判定の表駆動テスト

以下の「削除可」は、時刻以外の削除条件をすべて満たした場合を指す。「保護」は削除せず、変更対象には残すという意味である。日付は同一の固定日とし、テストでは現在時刻を注入する。各行に指定がない値は、番組開始 12:00、番組長 1,800 秒、保護設定有効、保護時間 5 分、固有マージンフラグ 1、開始・終了マージン 0 秒とする。

| ID | 設定・入力 | 現在時刻 | 期待結果・検知する漏れ |
| --- | --- | --- | --- |
| T01 | 二キー未設定、固有開始マージン 0 | 11:54:59 | 削除可。既定 5 分の直前 |
| T02 | T01 と同じ | 11:55:00 | 保護。開始＝期限の境界 |
| T03 | T01 と同じ | 11:55:01 | 保護。固定 60 秒が残っていれば失敗 |
| T04 | `CautionOnRecChange=0`、分数は 10 | 11:58:59 | 削除可。設定無効なら 1 分 |
| T05 | T04 と同じ | 11:59:00 | 保護。設定無効を無保護にしない |
| T06 | 有効、分数 0、マージン 0 | 11:59:59 / 12:00:00 | 順に削除可／保護。0 分を既定値へ戻さない |
| T07 | 有効、分数 10、マージン 0 | 11:50:00 | 保護。設定値を実際に参照 |
| T08 | 有効、5 分、固有開始マージン +120 秒 | 11:58:30 | 保護。文書 A-1 の録画中削除の例 |
| T09 | 固有フラグ 0、共通マージン +120 秒、固有欄 0 | 11:58:30 | 保護。共通値の参照 |
| T10 | 固有フラグ 1、固有マージン 0、共通 +600 秒 | 11:54:59 | 削除可。固有値優先 |
| T11 | 固有フラグ 0、共通 0、固有欄 +600 秒 | 11:54:59 | 削除可。無効な固有欄を使わない |
| T12 | 有効、5 分、固有開始マージン -120 秒 | 11:56:59 / 11:57:00 | 順に削除可／保護。負値は開始を遅らせる |
| T13 | 番組長 60 秒、固有開始マージン -120 秒 | 11:55:59 / 11:56:00 | 順に削除可／保護。実効マージン -60 秒への補正 |
| T14 | 番組長 0、開始マージン 0 | 11:54:59 / 11:55:00 | 順に削除可／保護。長さ 0 の境界 |
| T15 | T02 の終了マージンだけを正・負へ変更 | 11:55:00 | いずれも保護。開始判定に終了値を混ぜない |
| T16 | 変更前マージン 0 → 変更後 +600 秒、5 分保護 | 11:54:30 | 保護。変更前設定を使う誤りを検知 |
| T17 | 変更前マージン +600 秒 → 変更後 0、5 分保護 | 11:54:30 | 削除可。Windows の変更後設定基準と限界を確認 |
| T18 | 変更前は固有、変更後は既定、共通 +600 秒 | 11:54:30 | 保護。フラグも変更後を使う |
| T19 | 通常長・マージン 0、既に番組終了後 | 12:31:00 | 保護。終了済みを開始前と誤判定しない |
| T20 | 分数 `INT_MAX`、通常の予約日時 | 固定の正常日時 | 算術オーバーフローなし。期限が遠い将来となり保護 |

T01～T19 の基本ケースは EPG 予約とプログラム予約の双方に適用する。さらに期限との差が -1／0／+1 tick のケースを持ち、秒丸めや `>=` への変更を検知する。極端な `durationSecond` と負マージンでも、符号なしの減算・比較に化けないことを検証する。

### 7.2 設定読込・再読込テスト

- 本番の `LoadSetting()` をテスト用 INI に対して呼ぶ。キーなし、`1/5`、`0/10`、`1/0`、分数負値、空欄、非数値、`5junk`、`INT_MAX`、`INT_MAX` 超過を確認する。
- 不正値が既定値に戻り、入力 0 は有効な値として残ることを確認する。新しいメンバーの未初期化を見逃さない。
- 同じサーバー／ハーネス状態で 5 分 → 1 分 → 0 分 → キー削除による 5 分の復帰を再読込し、次の連動処理へ反映されることを確認する。
- 連動処理の途中では同じ設定スナップショット・現在時刻を使い、予約ごとに期限がずれないことを確認する。

### 7.3 共通処理への組み込み・回帰テスト

テストは単純な計算関数だけで終わらせない。少なくとも次の結果を、本体が使う処理に対する予約管理のテスト用記録先等で確認する。

| ID | 条件 | 必須の観測結果 |
| --- | --- | --- |
| I01 | EPG 自動予約、T08 の保護対象 | 削除 ID に含まれず、同じ ID が変更対象に残る |
| I02 | プログラム自動予約、T08 の保護対象 | I01 と同じ。変更後の録画設定・予約名を保持 |
| I03 | 削除可の予約と保護対象が同時に存在 | 削除可の ID だけ削除され、保護対象だけ変更される。重複した ID を送らない |
| I04 | 変更前予約は無効、変更後ルールは有効 | 無効状態を保持し、削除・再作成しない |
| I05 | 変更前予約は有効、変更後録画モードは無効 | 変更後の無効判定に従い削除せず変更する。変更前の `IsNoRec()` を使わない |
| I06 | `SyncResAutoAddChange=0` | 連動変更・削除なし。新保護設定は影響しない |
| I07 | `SyncResAutoAddChgNewRes=0` | 削除なし。従来の設定変更とタグ保持を維持 |
| I08 | 別の有効な自動予約にも一致 | 既存の保護条件を維持。時刻だけで削除しない |
| I09 | コメント空／末尾 `$` の個別予約 | 従来どおり連動対象外 |
| I10 | 固定チューナー分離の有効・無効、チューナー指定変更 | 変更前ルールに基づく対応付けを維持 |
| I11 | 無効な自動予約だけに属する予約 | 現行 Linux の除外順序・挙動を維持 |
| I12 | 自動予約ルール自体を削除／一般の予約削除 | 新しい期限判定が追加されていない |
| I13 | 複数ルールを一括変更し、同じ予約が重複して対応する | 既存の先に採用した設定・ID の一意性を維持 |
| I14 | 保護対象と新条件に一致する未予約番組が存在 | 保護対象の ID は維持し、新規番組の予約生成は既存どおり行う |

Lua の EPG／プログラム二経路と、版付き通信コマンドの EPG／プログラム二経路は、少なくとも代表的な保護ケース・削除可ケースで実行確認する。残る旧形式を含む全 7 経路についても共通関数への到達を追跡し、実行確認かコード確認かを区別して報告する。

検証用に WebUI 本体へボタンやエンドポイントを追加しない。実サーバーを使う統合検証が必要なら、設定・予約データ・ポート・保存先・チューナーを完全に隔離した環境を準備する。分離を確認できない状態では起動せず、ハーネスで検証し、実サーバー経路の未確認範囲を記録する。

### 7.4 テストの検出力・ビルド

次の誤実装が、それぞれ少なくとも一つのテストで失敗することを確認する。

- 固定 60 秒のままにする。
- 変更前予約で判定する。
- 共通マージンを無視する。
- 負マージンの下限補正を省く。
- `>` を `>=` に変える。
- 保護した予約まで `chgMap` から消す。

テスト用の一時変更で検出力を確認した場合は、正しい実装へ戻して最終テストを実行する。正しい式を別言語に書き写して成功しただけ、文字列検索でキーが見つかっただけ、ビルドだけ成功した場合は、動作検証済みとしない。

Linux の対象 Makefile は [EpgTimerSrv/EpgTimerSrv/Makefile](/home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv/Makefile)。後続実装でのビルド例は次のとおり。

```sh
make -C /home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv -j2
```

本体の Makefile は `-DNDEBUG` を付けるため、テストの合否判定を無効化される `assert` だけに依存させない。依存ライブラリ不足などでビルドできない場合は、失敗内容と未確認範囲を具体的に報告する。`make install`、`setup_ini`、本番サービスの再起動はこの検証手順に含めない。Windows 側は条件コンパイルの整合を確認し、ビルド環境がなければ Windows ビルド未実行と明記する。

## 8. Luna の実施順序と完了条件

1. 本書と現在の対象ソースを読み、作業ツリーの既存変更を確認する。過去の移植提案書より、本書と現在の実装を優先する。
2. Windows の判定条件を、第 4 節の式・入力元・境界と照合する。不一致を発見した場合は理由を文書に記録してから修正方針を確定する。
3. 設定読込 → 設定保持・再読込 → 共通連動処理 → 時刻判定 → 削除／変更先の振り分けまで実装する。
4. 第 7 節の検証を実施し、失敗した条件を直す。必要なビルドと最終差分確認を行う。
5. 下表に実装位置とテスト結果を記入して引き渡す。未実行・不一致を空欄や「問題なし」で隠さない。

| 要件 | 実装箇所（Luna が記入） | 検証結果（Luna が記入） |
| --- | --- | --- |
| INI 二キー、既定 5 分、無効時 1 分、0 分、不正値 | `EpgTimerSrvSetting.h/.cpp`。Linux だけにメンバーと厳密な整数読込を追加 | ソース確認済み。テスト用 INI の実行確認は未実施 |
| 起動時と再読込時の反映 | 既存 `LoadSetting()` → `ReloadSetting()` の設定スナップショット | 既存の再読込経路をコード確認。サービス再起動・実機再読込は未実施 |
| 変更後設定・無効状態による判定 | `SyncChangeAutoAddReserveData()` の `itr->second` | ソース確認済み。統合実行は未実施 |
| 固有／既定マージン、負値補正、64 bit 演算 | `CalcReserveStartTime()` | ソース確認済み。表駆動テストは未実施 |
| 厳密な `>` 境界、同一現在時刻・設定スナップショット | 同期処理内で一度だけ `protectTime` を算出 | ソース確認済み。境界の実行テストは未実施 |
| 保護対象を削除せず変更対象に残す | 保護対象を `chgMap` から消さず、削除候補だけ消去 | ソース確認済み。統合実行は未実施 |
| 既存の連動フラグ・他ルール・固定チューナー・タグを維持 | 既存の `chgMap` 構築・`HasEnabledAutoAddAfterRemove()` を維持 | 差分確認済み |
| Lua 二経路、新旧通信コマンドの全入口への適用 | 共通 `SyncChangeAutoAddReserveData()` に集約 | 呼出し経路をコード確認。各入口の実行は未実施 |
| 一般削除・ルール削除・新規追加への対象拡大なし | `syncChgNewRes` 内の削除判定だけを変更 | 差分確認済み |
| テストの検出力、Linux ビルド、Windows 条件コンパイル | Linux `make` を実施。新設定と判定は `_WIN32` で除外 | Linux ビルド成功。Windows ビルドと統合テストは未実施 |
| WebUI・配置済み設定への変更なし | WebUI、`/usr/local/lib/edcb/`、`/var/local/edcb/` は変更対象外 | 差分確認済み。インストール・サービス再起動は未実施 |

完了報告には、変更ファイル、上表、実行したテストと結果、ビルド結果、未実行項目、設定の既定値変更、T17 の Windows 互換上の限界を含める。第 7 節の必須検証に未実行があれば、その範囲を含めて「検証完了」とは報告しない。

### 実施結果（2026-09-07）

- 変更ファイルは `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.h`、`EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.cpp`、`EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp` の 3 ファイル。WebUI と配置済み設定は変更していない。
- Linux 用の既定値は `CautionOnRecChange=1`、`CautionOnRecMarginMin=5`。設定無効時は 1 分保護とし、0 分は有効値として扱う。不正な整数は既定値へ戻す。
- `make -C /home/oomugi413/git/EDCB/EpgTimerSrv/EpgTimerSrv -j2` は成功した。ビルド時の既存警告（`PathUtil.cpp`、`civetweb`）以外のエラーはない。
- 11:00 前の本番環境保護のため、`sudo make install`、`sudo systemctl restart edcb`、配置済み INI の編集、実サーバー上の予約操作は行っていない。GPT から `sudo` は実行していない。
- 20 件の時刻表駆動テスト、14 件の統合テスト、Windows ビルド、実機での再読込は未実施である。したがって A-1 はソース実装と Linux ローカルビルドまで完了し、実機反映・動作検証は 11:00 以降の別作業とする。
