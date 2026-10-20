# 2026-06-28 04:15「レインマン（吹替版）」録画開始失敗の調査メモ

## 前提

- EDCB / Mirakurun の再起動は行っていない。
- `systemctl restart`、`pm2 restart`、`pm2 reload`、Mirakurun / EDCB の停止を伴う操作は行っていない。
- 調査対象は EDCB の録画結果・通知ログ・設定、Mirakurun の API / 設定、journal、PM2 ログ取得可否。

## 対象番組

- 番組名: `[映]レインマン（吹替版）[字]`
- 予定時刻: `2026/06/28 04:15:00 - 06:30:00`
- サービス: `ＷＯＷＯＷプライム`
- Network / TS / Service / Event:
  - `ONID=4`
  - `TSID=16432`
  - `SID=191`
  - `EventID=58204`

## 確認できた事実

### EDCB 録画結果

`/var/local/edcb/Setting/RecInfo.txt` と `http://127.0.0.1:5510/api/EnumRecInfo` で、同じ番組の失敗結果が 2 件残っていた。

| RecInfo ID | 番組 | recFilePath | drops | scrambles | recStatus | comment |
| --- | --- | --- | ---: | ---: | ---: | --- |
| 8 | `[映]レインマン（吹替版）[字]` | 空 | 0 | 0 | 12 | `録画開始処理に失敗しました` |
| 9 | `[映]レインマン（吹替版）[字]` | 空 | 0 | 0 | 12 | `録画開始処理に失敗しました` |

録画ファイルパスが空で、drop / scramble も 0 のため、TS を受信し始めてから壊れたというより、録画開始処理のかなり早い段階で失敗した状態に見える。

### EDCB 通知ログ

`/var/local/edcb/EpgTimerSrvNotify.log` では、対象番組に対して 2 回の開始準備と 2 回の失敗終了が出ている。

```text
2026/06/28 03:55:00.119 [予約録画開始準備] BonDriver_LinuxMirakc_S.so
2026/06/28 03:59:36.870 [予約録画開始準備] ナショジオ 2026/06/28 04:00:00～ 大解剖！...
2026/06/28 03:59:56.280 [録画開始] ナショジオ 2026/06/28 04:00:00～ 大解剖！...

2026/06/28 04:10:00.071 [予約録画開始準備] BonDriver_LinuxMirakc_S.so
2026/06/28 04:14:36.769 [予約録画開始準備] ＷＯＷＯＷプライム 2026/06/28 04:15:00～ [映]レインマン（吹替版）[字]
2026/06/28 04:14:55.822 [録画終了] ＷＯＷＯＷプライム 2026/06/28 04:15～06:30 [映]レインマン（吹替版）[字] 録画開始処理に失敗しました

2026/06/28 04:14:57.075 [予約録画開始準備] BonDriver_LinuxMirakc_S.so
2026/06/28 04:14:57.076 [予約録画開始準備] ＷＯＷＯＷプライム 2026/06/28 04:15:00～ [映]レインマン（吹替版）[字]
2026/06/28 04:15:02.829 [録画終了] ＷＯＷＯＷプライム 2026/06/28 04:15～06:30 [映]レインマン（吹替版）[字] 録画開始処理に失敗しました
```

同じ番組の録画結果が 2 件あることと一致しており、少なくとも EDCB 側では対象イベントに対する開始処理が短時間に 2 回走っている。これは後述のとおり、ただちに人為的な二重予約を意味しない。

また、同じ `ＷＯＷＯＷプライム` / `BS 16432` については後続番組で録画開始に成功している。

```text
2026/06/28 06:29:36.250 [予約録画開始準備] ＷＯＷＯＷプライム 2026/06/28 06:30:00～ [映]マイノリティ・リポート（吹替版）...
2026/06/28 06:29:55.385 [録画開始] ＷＯＷＯＷプライム 2026/06/28 06:30:00～ [映]マイノリティ・リポート（吹替版）...
```

そのため、WOWOW Prime のチャンネル定義や BS 16432 の基本的なチューニング設定が恒久的に壊れている可能性は低い。

### EDCB デバッグログ

`/var/local/edcb/EpgTimerSrvDebugLog.txt` と、コピーされた `.codex/EpgTimerSrvDebugLog.txt` を確認した。04:15 の失敗原因を直接示す BonDriver / Mirakurun / `StartSave()` のエラー詳細は出ていなかった。

関連する周辺ログは以下。

```text
[260628041340.284] Start Load EpgData
[260628041340.284] EgpDataCap3 [InitializeEP : id=10]
[260628041342.934] EgpDataCap3 [UnInitializeEP : id=10]
[260628041342.945] End Load EpgData 2661msec
[260628041343.527] Done PostLoad EpgData 581msec
[260628041456.417] Start ReloadBankMap
[260628041456.417] End ReloadBankMap 0msec
[260628041456.417] Done PostLoad EpgData 583msec
[260628041503.411] Done PostLoad EpgData 570msec
[260628041532.957] Shutdown cancelled
```

`04:14:56.417` の `Start ReloadBankMap` は、通知ログの 1 回目失敗 `04:14:55.822` と 2 回目開始準備 `04:14:57.075` の間にある。さらに同じミリ秒で `Done PostLoad EpgData` が出ているため、PostLoad の自動予約チェック中に予約が追加され、`AddReserveData()` から `ReloadBankMap()` が走った可能性が高い。

一方、`RetryOtherTuners` の経路なら `ReserveManager.cpp` の `CHECK_ERR_OPEN` 分岐で `●予約(ID=...)にNGチューナー...` の debug log が出るはずだが、コピー版 `EpgTimerSrvDebugLog.txt` には該当行がない。この点でも、04:14:56 の `ReloadBankMap` を `RetryOtherTuners` そのものと見る根拠は弱い。

06:30 のマイノリティ・リポート正常時は以下。

```text
[260628062840.731] Start Load EpgData
[260628062843.982] Done PostLoad EpgData 578msec
[260628063003.871] Done PostLoad EpgData 584msec
[260628063006.280] ●予約(ID=61)のEIT[present]を確認しました
[260628063033.418] Shutdown cancelled
```

この時間帯には 04:14:56 のような `Start ReloadBankMap` は出ていない。現在の `Reserve.txt` でも `ID=61` は `[映]マイノリティ・リポート（吹替版）[SS][字]` で、コメントは `EPG自動予約(T・クルーズ)`。同じ自動予約条件の番組でも、06:30 は再投入や再割り当てなしに録画開始へ進んだと見られる。

なお、`06:59:33.874` に `Deny TCP cmd:0x20544547` があるが、失敗時刻から 2 時間以上後であり、今回の 04:15 失敗とは直接関係しない。

なお、`/var/local/edcb/EpgDataCap_Bon.ini` では以下の設定だった。

```ini
SaveDebugLog=0
TraceBonDriverLevel=0
```

このため、EpgDataCap_Bon / BonDriver 側の詳細な失敗理由は EDCB の通常ログには残りにくい設定になっている。

### EDCB 設定

`/var/local/edcb/EpgTimerSrv.ini` では以下を確認した。

- `RetryOtherTuners=1`
- `RecAppWakeTime=5`
- `[BonDriver_LinuxMirakc_S.so] Count=4`
- `[BonDriver_LinuxMirakc_S3.so] Count=2`

`/var/local/edcb/Setting/ChSet5.txt` と BonDriver 側の ChSet4 では、WOWOW Prime は `ONID=4 / TSID=16432 / SID=191` として定義されている。

### Mirakurun の状態

Mirakurun API は `http://127.0.0.1:40772` で応答した。

`/api/status` で確認できた主な値:

- version: `4.1.1-om1`
- pid: `3092`
- server config: `/usr/local/etc/mirakurun/server.yml`
- tuners config: `/usr/local/etc/mirakurun/tuners.yml`
- channels config: `/usr/local/etc/mirakurun/channels.yml`
- errorCount:
  - `uncaughtException=334`
  - `tunerDeviceRespawn=3`
  - `bufferOverflow=0`
  - `unhandledRejection=0`

この `errorCount` は現在時点の累積値で、04:15 の失敗と直接対応するかは PM2 ログなしでは判断できない。

`/api/tuners` では、調査時点で PX_S2 が以下のように `BS 16432` を配信中だった。

```text
recpt1 --device /dev/px4video1 16432 - -
/api/channels/BS/16432/stream?decode=1
```

これは 06:30 開始の後続 WOWOW Prime 録画と見られ、同じチャンネルを Mirakurun 経由で現在は開けていることを示す。

`/usr/local/etc/mirakurun/tuners.yml` では PX_S1 - PX_S4 が BS/CS 用として有効で、`/usr/local/etc/mirakurun/channels.yml` でも BS `16432` は有効だった。

### PM2 ログ

Mirakurun は `sudo pm2 start mirakurun-server` で動作しているとのことなので、当初は root 側の PM2 ログ確認を試したが、この環境からは sudo の対話認証が必要で読めなかった。

その後、ユーザー提示の `.codex/mirakurunlog.txt` を確認した。

#### 04:15 レインマン（吹替版）の失敗時

Mirakurun 側では、04:10 前に対象チャンネル `BS 16432` の stream が開けている。

```text
2026-06-28T04:09:59.505+09:00 info: 127.0.0.1 - GET /api/channels HTTP/1.0 200 - - 3.952 ms -
2026-06-28T04:09:59.709+09:00 info: TSDecoder#84 has created (command=arib-b25-stream-test)
2026-06-28T04:09:59.716+09:00 info: TSDecoder#84 process has spawned by command `arib-b25-stream-test` (pid=681858)
2026-06-28T04:09:59.721+09:00 info: TunerDevice#0 process has spawned by command `recpt1 --device /dev/px4video0 16432 - -` (pid=681860)
2026-06-28T04:09:59.721+09:00 info: TunerDevice#0 streaming to user `127.0.0.1:35868` (priority=102)
```

しかし、EDCB 側の失敗ログとほぼ同時刻に stream が閉じている。

```text
2026-06-28T04:14:55.813+09:00 info: TSDecoder#84 has closed (command=arib-b25-stream-test)
2026-06-28T04:14:55.814+09:00 info: TunerDevice#0 end streaming to user `127.0.0.1:35868` (priority=102)
2026-06-28T04:14:55.814+09:00 info: 127.0.0.1 - GET /api/channels/BS/16432/stream?decode=1 HTTP/1.0 200 - - 355.878 ms -
2026-06-28T04:14:55.814+09:00 info: TSDecoder#84 process has closed with exit code=0 by signal `SIGKILL` (pid=681858)
```

続けて 2 回目の開始処理も走っている。

```text
2026-06-28T04:14:57.005+09:00 info: TSDecoder#85 has created (command=arib-b25-stream-test)
2026-06-28T04:14:57.011+09:00 info: TSDecoder#85 process has spawned by command `arib-b25-stream-test` (pid=708479)
2026-06-28T04:14:57.011+09:00 info: TunerDevice#0 streaming to user `127.0.0.1:39756` (priority=102)
2026-06-28T04:15:02.820+09:00 info: TSDecoder#85 has closed (command=arib-b25-stream-test)
2026-06-28T04:15:02.820+09:00 info: TunerDevice#0 end streaming to user `127.0.0.1:39756` (priority=102)
2026-06-28T04:15:02.821+09:00 info: 127.0.0.1 - GET /api/channels/BS/16432/stream?decode=1 HTTP/1.0 200 - - 69.267 ms -
2026-06-28T04:15:02.822+09:00 info: TSDecoder#85 process has closed with exit code=0 by signal `SIGKILL` (pid=708479)
2026-06-28T04:15:05.833+09:00 info: TunerDevice#0 process has closed with exit code=0 by signal `null` (pid=681860)
2026-06-28T04:15:05.934+09:00 info: TunerDevice#0 released
```

04:10 - 04:15 付近に `warn`、`error`、`uncaught`、`respawn`、`EPIPE`、`ECONN` などの Mirakurun 側異常ログは見つからなかった。

また、`TSDecoder ... closed with exit code=0 by signal SIGKILL` はこのログ内では通常の stream 終了時にも出ているため、この行だけを Mirakurun 異常とは見なせない。

#### 06:30 マイノリティ・リポート（吹替版）の正常時

06:30 開始の後続 WOWOW Prime 録画では、同じ `BS 16432` に対して 06:25 に stream が開かれている。

```text
2026-06-28T06:24:59.987+09:00 info: 127.0.0.1 - GET /api/channels HTTP/1.0 200 - - 3.899 ms -
2026-06-28T06:25:00.191+09:00 info: TSDecoder#87 has created (command=arib-b25-stream-test)
2026-06-28T06:25:00.197+09:00 info: TSDecoder#87 process has spawned by command `arib-b25-stream-test` (pid=1411912)
2026-06-28T06:25:00.202+09:00 info: TunerDevice#1 process has spawned by command `recpt1 --device /dev/px4video1 16432 - -` (pid=1411913)
2026-06-28T06:25:00.202+09:00 info: TunerDevice#1 streaming to user `127.0.0.1:49164` (priority=102)
```

この `127.0.0.1:49164` の stream は、提示されたログ末尾まで `end streaming` が出ていない。EDCB 通知ログ上も `06:29:55.385` に `[録画開始]` しているため、Mirakurun 側 stream が維持されたまま録画開始へ移行できている。

なお、06:30 付近には以下の終了ログもあるが、これは `BS 16625` の別 stream の終了であり、06:30 開始の `BS 16432` / マイノリティ・リポートではない。

```text
2026-06-28T06:30:02.299+09:00 info: TunerDevice#0 end streaming to user `127.0.0.1:41216` (priority=102)
2026-06-28T06:30:02.299+09:00 info: 127.0.0.1 - GET /api/channels/BS/16625/stream?decode=1 HTTP/1.0 200 - - 690.277 ms -
```

#### PM2 ログから見た比較

| 観点 | 04:15 レインマン失敗 | 06:30 マイノリティ・リポート正常 |
| --- | --- | --- |
| チャンネル | `BS 16432` | `BS 16432` |
| Mirakurun stream 開始 | `04:09:59` | `06:25:00` |
| recpt1 起動 | `recpt1 --device /dev/px4video0 16432 - -` 成功 | `recpt1 --device /dev/px4video1 16432 - -` 成功 |
| decoder 起動 | `arib-b25-stream-test` 起動成功 | `arib-b25-stream-test` 起動成功 |
| EDCB らしき接続 | `127.0.0.1:35868`、直後に `127.0.0.1:39756` | `127.0.0.1:49164` |
| stream の維持 | 録画開始タイミングで 2 回とも閉じた | 提示ログ末尾まで維持 |
| Mirakurun エラー | 該当時刻に確認できず | 該当時刻に確認できず |

この比較から、04:15 の失敗は「Mirakurun が `BS 16432` を開けなかった」ものではない。少なくとも 04:09:59 の時点で `recpt1` と decoder は起動し、EDCB らしき localhost クライアントへ stream を提供している。

差分は、レインマンでは録画開始時刻直前から直後にかけて EDCB 側の接続が閉じ、マイノリティ・リポートでは同じチャンネルの接続が維持されて録画開始に進んでいる点。

`journalctl` では 04:00 - 04:45 の範囲で `mirakurun` / `pm2` / `EpgTimer` / `EpgData` / `tuner` / `Bon` / `error` / `fail` などを検索したが、該当ログは見つからなかった。

### EDCB / BonDriver ソース確認後の補足

EDCB と Mirakurun の接続には、`~/git/BonDriver_LinuxMirakc/BonDriver_LinuxMirakc.so` と同一 SHA-256 のバイナリが `/usr/local/lib/edcb/BonDriver_LinuxMirakc_S.so` / `BonDriver_LinuxMirakc_S3.so` として使われている。

`/usr/local/lib/edcb/BonDriver_LinuxMirakc_S.so.ini` と `S3.so.ini` は以下。

```ini
SERVER_HOST="127.0.0.1"
SERVER_PORT=40772
SERVER_TYPE="http"
DECODE_B25=1
PRIORITY=102
SERVICE_SPLIT=0
```

PM2 ログの `priority=102` と一致している。

#### RetryOtherTuners について

`RetryOtherTuners=1` は有効だが、ソース上は「チューナーのオープンに失敗したとき」だけ再割り当てされる。

- `EpgTimerSrv/EpgTimerSrv/ReserveManager.cpp`
  - `ProcessRecEnd()` 内の `1327-1331` 行付近で、`CHECK_ERR_OPEN` のときだけ `AddNGTunerID()` して `continue` する。
  - このとき `●予約(ID=...)にNGチューナー(ID=...)を追加します` が debug log に出る。
  - `1381-1386` 行付近で、`CHECK_ERR_RECSTART` / `CHECK_ERR_CTRL` は `REC_END_STATUS_ERR_RECSTART`、`CHECK_ERR_OPEN` は `REC_END_STATUS_OPEN_ERR` に分岐する。
  - `1415` 行付近で、通常の録画終了処理として対象予約が `DelReserve()` される。
- `Common/StructDef.h`
  - `75` 行付近が `REC_END_STATUS_OPEN_ERR`、`85` 行付近が `REC_END_STATUS_ERR_RECSTART`。
  - `129` 行付近と `139` 行付近で、それぞれ `チューナーのオープンに失敗しました` / `録画開始処理に失敗しました` のコメント文字列になる。
- `Document/Readme_Mod.txt` でも、この設定は「録画結果が `チューナーのオープンに失敗` となる場合」の再割り当てと説明されている。

今回の録画結果は 2 件とも `recStatus=12`、つまり `録画開始処理に失敗しました` であり、`チューナーのオープンに失敗しました` ではない。

そのため、2 回の結果は `RetryOtherTuners` の通常経路そのものでは説明しにくい。少なくとも、1 回目の結果が `OPEN_ERR` として処理され、内部的に NG チューナー追加だけで済んだ、という動きではない。コピー版 debug log にも `NGチューナー` 追加ログは見つからない。

#### BonDriver_LinuxMirakc の動き

`BonDriver_LinuxMirakc` の `OpenTuner()` は、Mirakurun への接続確認と `/api/channels` 取得によるチャンネル初期化を行う。実際の stream は `SetChannel()` で `/api/channels/{type}/{channel}/stream?decode=1` を開く。

ソース上の流れ:

- `~/git/BonDriver_LinuxMirakc/src/BonDriver_LinuxMirakc.cpp`
- `OpenTuner()` (`144-181` 行付近)
  - `conn->connect()`
  - `InitChannel()`
  - `/api/channels` 取得
- `SetChannel()` (`396-445` 行付近)
  - `/api/channels/BS/16432/stream?decode=1` へ GET
  - HTTP 200 なら受信スレッドを作成
  - HTTP 200 以外、または request 失敗なら `Tuner unavailable` で `FALSE`

PM2 ログでは 04:15 失敗時も 06:30 正常時も `BS 16432` の stream HTTP request は 200 で成立している。したがって、少なくとも BonDriver の `SetChannel()` が HTTP 失敗で `FALSE` になった可能性は低い。

#### 録画開始処理の失敗点

EDCB 側の `録画開始処理に失敗しました` は、主に以下の経路で出る。

1. `CTunerBankCtrl::RecStart()` が呼ばれる。
2. `RecStart()` 内で EpgDataCap_Bon に `CMD2_VIEW_APP_REC_START_CTRL` を送る。
3. EpgDataCap_Bon 側は `bonCtrl.StartSave()` が `TRUE` の場合だけ `CMD_SUCCESS` を返す。
4. `SendViewStartRec()` が成功しないと、`CHECK_ERR_RECSTART` になり、録画結果は `REC_END_STATUS_ERR_RECSTART` になる。

つまり今回の `recStatus=12` は、Mirakurun stream が開けないことより、EpgDataCap_Bon / BonCtrl / Write プラグイン側で「保存開始」に失敗した可能性が高い。

`StartSave()` の下流では以下が失敗候補になる。

- `EpgTimerSrv/EpgTimerSrv/TunerBankCtrl.cpp`
  - `360-403` 行付近で、録画開始時刻に `RecStart()` が `false` なら `CHECK_ERR_RECSTART` になる。
  - `907` 行付近で、`SendViewStartRec(param)` が成功しないと `RecStart()` が失敗扱いになる。
- `EpgDataCap_Bon/EpgDataCap_Bon/EpgDataCap_BonMin.cpp`
  - `705-715` 行付近で、`CMD2_VIEW_APP_REC_START_CTRL` に対して `bonCtrl.StartSave()` が `TRUE` の場合だけ `CMD_SUCCESS` を返す。
- `BonCtrl/TSOut.cpp`
  - `755-758` 行付近で、`ctrlID` が `serviceUtilMap` に存在しないと `FALSE`。
- `BonCtrl/OneServiceUtil.cpp`
  - `221-239` 行付近で、すでに録画中またはぴったり録画待機中なら `FALSE`。
- `BonCtrl/WriteTSFile.cpp`
  - `41-44` 行付近で保存先リストなしなら `FALSE`。
  - `156-164` 行付近で Write プラグイン初期化失敗。
  - `181-198` 行付近で Write プラグインの `Start()` 失敗。
  - `214-221` 行付近で全出力先失敗。
- `Write_Default/Write_Default/WriteMain.cpp`
  - `60-82` 行付近で録画ファイル作成に失敗すると `FALSE`。
  - `87-96` 行付近で `KeepDisk=1` 相当の事前確保を行うが、Linux の `fallocate(..., FALLOC_FL_KEEP_SIZE, ...)` の戻り値はこの箇所では録画開始失敗として扱われていない。

現在の設定では以下を確認した。

- 録画名: `RecName_Macro.so`
- マクロ: `$SDYYYY$$SDMM$$SDDD$_$Title$$SubTitle2$.ts`
- `RecOverWrite=0`
- `KeepDisk=1`
- 既定録画先:
  - `/mnt/recording`
  - `/mnt/recording/EDCB`
- `/mnt/recording` は調査時点で約 18TB 空き。
- `/mnt/recording` と `/mnt/recording/EDCB` は `drwxrwxrwx`。
- `20260628_[映]マイノリティ・リポート（吹替版）[SS][字].ts` は `/mnt/recording` に作成済み。
- `レインマン` の録画ファイルは見つからなかった。

よって、恒常的な保存先容量不足・権限不足は弱い。ただし、04:15 のその瞬間に `Write_Default` がどの errno で失敗したかは、EpgDataCap_Bon 側の debug log が無効なため確認できない。

#### 2 回の開始処理について

1 回目の失敗と 2 回目の開始準備の間に、EDCB debug log では以下が出ている。

```text
[260628041456.417] Start ReloadBankMap
[260628041456.417] End ReloadBankMap 0msec
[260628041456.417] Done PostLoad EpgData 583msec
```

これは通知ログ上の以下の間に位置する。

```text
2026/06/28 04:14:55.822 [録画終了] ... レインマン ... 録画開始処理に失敗しました
2026/06/28 04:14:57.076 [予約録画開始準備] ... レインマン
```

EDCB ソースでは、EPG データ読み込み後の PostLoad 処理で自動予約チェックが走り、条件に合う番組が現在予約に存在しなければ `AddReserveData()` される。

また、録画開始失敗時は `ProcessRecEnd()` で録画結果を追加したあと、対象予約を `DelReserve()` する。一方、成功録画として `RecInfo2` に登録されるのは正常終了かつ録画ファイルパスがある場合で、今回のような開始失敗は `RecInfo2` には入らない。

ソース上の対応箇所:

- `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp`
  - `817-837` 行付近で EPG 自動予約・プログラム自動予約を走査し、追加対象があれば `reserveManager.AddReserveData()` を呼ぶ。
  - `864` 行付近で `Done PostLoad EpgData ...` を出す。
  - `1816` 行付近で、EPG 自動予約の検索範囲は `now` から `now + autoAddHour`。
  - `1825-1827` 行付近で、現在予約に同一イベントが存在しない場合だけ追加候補になる。
- `EpgTimerSrv/EpgTimerSrv/ReserveManager.cpp`
  - `258` 行付近で予約を追加し、`272` 行付近で `ReloadBankMap(minStartTime)` を呼ぶ。

したがって、2 回の開始処理は「ユーザーが同一番組を二重予約していた」と断定するより、以下のほうが自然。

1. 04:14:55 に 1 回目の録画開始処理が失敗。
2. その失敗により予約が削除され、録画結果だけが残る。
3. 直後の PostLoad / 自動予約チェックで、同じ `EventID=58204` がまだ検索条件に合い、かつ現在予約に存在しないため、再び予約追加される。この時刻は `04:14:56` で、番組開始 `04:15:00` より前なので、`now` 以降の検索対象になり得る。
4. 04:14:57 に 2 回目の開始準備が走り、04:15:02 に同じく開始失敗。
5. 04:15:03 にも PostLoad は完了しているが、ここでは `Start ReloadBankMap` が出ていない。番組開始時刻を過ぎたため同じ番組が再々追加されなかった可能性がある。

これは `RetryOtherTuners` そのものではないが、「同一番組を人為的に二重予約していた」という意味でもない。自動予約と失敗後削除、PostLoad のタイミングが重なった再投入の可能性がある。

## 考えられる原因

### 1. 1 回目失敗直後の自動予約再追加

最も強く疑われる。

根拠:

- 録画結果に同一イベントの失敗が 2 件ある。
- EDCB 通知ログでも、対象番組の開始準備と失敗終了が 2 回出ている。
- 1 回目は `04:14:55.822` に失敗し、直後の `04:14:57.075` に再度 BonDriver 準備が走り、`04:15:02.829` に再び失敗している。
- PM2 ログでも、EDCB らしき localhost 接続が `127.0.0.1:35868` と `127.0.0.1:39756` の 2 本として現れ、どちらも短時間で閉じている。
- 1 回目の失敗直後、2 回目の開始準備直前に `Start ReloadBankMap` / `Done PostLoad EpgData` が同じミリ秒で出ている。
- コピー版 `EpgTimerSrvDebugLog.txt` には `RetryOtherTuners` の `NGチューナー` 追加ログがない。
- ソース上、開始失敗時は対象予約が削除されるが、成功録画としての `RecInfo2` には登録されない。
- EPG PostLoad 後の自動予約チェックは、現在予約に存在しない対象イベントを再追加し得る。

同じ `ONID=4 / TSID=16432 / SID=191 / EventID=58204` に対する 2 回の開始処理は、単純な二重予約ではなく、1 回目の開始失敗後に自動予約が同じイベントを再投入した可能性がある。

これはユーザー指摘のとおり、人為的な二重予約とは限らない。ただし、ソース上 `RetryOtherTuners` が直接この 2 件の `recStatus=12` を作ったとは言いにくく、より近い説明は「失敗後削除」と「PostLoad 自動予約再追加」の競合。

### 2. EDCB 側の録画開始処理での失敗

直接原因として最も疑わしい。

根拠:

- EDCB 側では録画ファイルパスが空で、録画開始の早い段階で失敗している。
- PM2 ログでは 04:09:59 に `recpt1 --device /dev/px4video0 16432 - -` と `arib-b25-stream-test` が起動できている。
- 失敗時刻付近の Mirakurun ログに `error` / `warn` / `uncaught` / `respawn` は見当たらない。
- 04:14:55 と 04:15:02 の stream 終了は、EDCB 通知ログの失敗時刻と一致している。
- 06:30 のマイノリティ・リポートでは、同じ `BS 16432` の stream が維持され、録画開始に進んでいる。
- ソース上、`recStatus=12` は `SendViewStartRec()` 失敗、または録画制御作成失敗で発生する。

このため、04:15 の時点で Mirakurun がチャンネルを開けなかったというより、EDCB / EpgDataCap_Bon / BonDriver 呼び出し側が、録画開始へ移行する段階で stream を閉じたように見える。

具体的には、以下のような EDCB 側の開始処理失敗が候補になる。

- 1 回目失敗直後の再投入により、同一イベント・同一出力先に近い条件で再度開始処理が走った。
- 録画ファイル名または録画先パスの生成・作成失敗
- EpgDataCap_Bon 側での録画開始移行失敗
- BonDriver 経由の stream は開けたが、録画保存開始時の内部状態が異常になった
- `ctrlID` に対応する service control が EpgDataCap_Bon 側に存在しない、または既に録画中扱いだった。

ただし、EDCB 側の詳細ログが `SaveDebugLog=0` / `TraceBonDriverLevel=0` で残っていないため、どの処理で失敗したかまでは確定できない。

### 3. 04:15 時点の Mirakurun / tuner の一時的なオープン失敗

PM2 ログ確認後は可能性が下がった。

根拠:

- 04:09:59 に `BS 16432` の `recpt1` 起動は成功している。
- decoder も起動している。
- Mirakurun 側の該当時刻ログに明確なエラーがない。
- 06:25 からの同じ `BS 16432` の stream は正常に維持されている。

Mirakurun の現在の `errorCount` に `uncaughtException=334`、`tunerDeviceRespawn=3` がある点は気になるが、提示された PM2 ログ内では 04:15 の失敗と対応するログは確認できなかった。

### 4. チューナー不足またはチューナー割り当て競合

可能性はあるが、現時点の証拠は弱い。

根拠:

- 04:00 から `ナショジオ` の録画が進行しており、同じ `BonDriver_LinuxMirakc_S.so` 系の衛星チューナーが使われていた。
- 対象番組が失敗後に再投入された場合、04:15 近辺に同じイベントの開始処理が短時間に連続した。
- PM2 ログ上では 04:15 時点で `ナショジオ` と見られる CS stream も 05:00 まで正常に継続しており、対象番組用には別の `TunerDevice#0` が使われていた。

ただし、設定上は `BonDriver_LinuxMirakc_S.so Count=4` で、Mirakurun 側にも PX_S1 - PX_S4 の BS/CS チューナーが有効に見える。現在の EDCB `EnumTunerReserveInfo` でも `チューナー不足 total=0` だった。

そのため、単純な恒常的チューナー不足というより、失敗後再投入や EDCB 側の録画開始処理失敗のほうが自然に見える。

### 5. チャンネル定義や WOWOW Prime 設定の不備

可能性は低い。

根拠:

- EDCB の ChSet で WOWOW Prime は `4 / 16432 / 191` と定義されている。
- Mirakurun の service / channel にも BS `16432` と WOWOW Prime service が存在している。
- 同じ WOWOW Prime の後続録画が 06:30 に開始できている。
- PM2 ログでも 04:15 失敗時と 06:30 正常時の両方で `BS 16432` の `recpt1` 起動に成功している。

## 追加確認候補

再起動なしで確認できる範囲としては、EDCB 側の再投入経路と、録画ファイル作成まわりの確認が優先。

- 対象番組を登録した自動予約登録条件の確認
- 同一 `EventID=58204` が失敗後に自動予約で再追加された経路の確認
- 録画保存先ディレクトリの空き容量・権限・ファイル名生成ルールの確認
- EDCB の過去ログやバックアップに、対象予約の `Reserve.txt` 相当が残っていないか確認
- EpgDataCap_Bon の詳細ログを有効化できる時間帯に、`CMD2_VIEW_APP_REC_START_CTRL`、`CWriteTSFile::StartSave Err ...`、`CWriteMain::Start Err:errno=...` が出るか確認
- 今後の切り分け用に、録画がない時間帯で `EpgDataCap_Bon.ini` の `SaveDebugLog` / `TraceBonDriverLevel` を上げることを検討

最後の設定変更は再起動またはプロセス再読み込みが必要になる可能性があるため、録画中には行わない。

## まとめ

PM2 ログとソース確認を踏まえると、現時点で最も疑わしいのは、録画開始へ移行する段階の EDCB / EpgDataCap_Bon / Write プラグイン側の保存開始失敗。

04:15 のレインマンでも Mirakurun は `BS 16432` の `recpt1` と decoder を起動できており、明確な Mirakurun エラーは出ていない。06:30 のマイノリティ・リポートでは同じ `BS 16432` の stream が維持されて録画開始できているため、Mirakurun のチャンネル定義ミス、恒久的な WOWOW Prime 受信不能、単純な tuner open 失敗は可能性が低い。

2 回の開始処理については、人為的な二重予約とは断定しない。`RetryOtherTuners` の通常経路とも一致せず、1 回目の開始失敗で予約が削除された直後、PostLoad の自動予約チェックが同じイベントを再追加した可能性がある。

残る確認ポイントは、EpgDataCap_Bon 側で `StartSave()` がなぜ失敗したか。現在のログ設定ではその errno や内部理由が残っていないため、再発時または録画のない時間帯に詳細ログを有効化して確認する必要がある。
