# KonomiTV 録画中判定エラー確認と対策案 2

## 前提

対象は `~/git/KonomiTV` の EDCB 録画中判定と、`linux_reserve_change.md` で追加した `SyncResAutoAdd*` 関連処理の関係。

録画中環境への影響を避けるため、`systemctl start` / `systemctl stop` / `systemctl restart` は実行していない。確認は、KonomiTV と EDCB のソース確認、ログ確認、実行中 EDCB への読み取り API だけで行った。

## KonomiTV 側の確認

KonomiTV の設定は EDCB backend で、CtrlCmd 接続先は TCP だった。

```yaml
backend: 'EDCB'
edcb_url: 'tcp://127.0.0.1:4510/'
```

録画予約一覧 API の録画中判定は、`server/app/routers/ReservationsRouter.py` の次の経路。

- `ShouldCheckRecordingInProgress()`
  - 無効予約、視聴予約は判定対象外
  - 現在時刻の前後2時間に重なる予約だけ追加問い合わせ対象
- `GetIsRecordingInProgress()`
  - `CtrlCmdUtil.sendGetRecFilePath(reserve_id)` が `str` を返せば録画中
  - それ以外は録画中ではない扱い

番組表側も `server/app/routers/ProgramsRouter.py` で同様に `sendGetRecFilePath(reserve_id)` の戻り値だけを見て `Recording` / `Reserved` を決めている。

`server/app/utils/edcb/CtrlCmdUtil.py` の `sendGetRecFilePath()` は、EDCB の `CMD_EPG_SRV_NWPLAY_TF_OPEN`、つまりコマンド番号 `1087` を送る。成功時は `NWPlayTimeShiftInfo.file_path` を返し、失敗時は `None` を返す。

一方、KonomiTV には `EDCBUtil.getEDCBStatus()` もあり、これは `sendGetNotifySrvStatus()` の `param1` を見て `Normal` / `Recording` / `EPGGathering` を返す。ただし、予約一覧や番組表の個別予約の録画中表示は、この全体ステータスではなく `sendGetRecFilePath()` に依存している。

## EDCB 側の確認

KonomiTV が使う録画中判定の EDCB 側経路は次。

- `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp`
  - `CMD2_EPG_SRV_NWPLAY_TF_OPEN`
  - `reserveManager.GetRecFilePath(reserve_id, filePath)` が成功したときだけタイムシフトを開く
- `EpgTimerSrv/EpgTimerSrv/ReserveManager.cpp`
  - `CReserveManager::GetRecFilePath()`
  - 各 `TunerBankCtrl` に問い合わせる
- `EpgTimerSrv/EpgTimerSrv/TunerBankCtrl.cpp`
  - `CTunerBankCtrl::GetRecFilePath()`
  - 対象 `reserve_id` がチューナーバンク上に存在し、状態が `TR_REC` で、`recFilePath` が空でない場合だけ成功する

`linux_reserve_change.md` の変更は、この `NWPLAY_TF_OPEN`、`ReserveManager::GetRecFilePath()`、`TunerBankCtrl::GetRecFilePath()` 自体は直接変更していない。

ただし `TunerBankCtrl` 側には、録画開始後に `SendViewGetRecFilePath()` で `recFilePath` を埋める処理があり、コメント上も「ぴったり録画は取得成功が遅れる」とされている。このため、`sendGetRecFilePath()` は「録画中かどうか」の唯一の根拠にすると開始直後に弱い。

## 実機 API 確認

EDCB は pid `2451905` で起動しており、以下の待ち受けがあった。

```txt
0.0.0.0:4510  EpgTimerSrv CtrlCmd TCP
0.0.0.0:5510  EpgTimerSrv HTTP API
```

HTTP API は正常に応答した。

```txt
EnumReserveInfo: HTTP 200, 0.010630 sec, 99124 bytes
EnumAutoAdd:     HTTP 200, 0.009518 sec, 75399 bytes
```

KonomiTV と同じ `CtrlCmdUtil` を使い、`tcp://127.0.0.1:4510/` へ読み取り問い合わせを行った結果は次。

```txt
now=2026-06-27T21:45:23+09:00
notify_param1=0
notify_count=35
reserves=136
tuners=11
check_candidates=0
tuner id=1 count=18 first_ids=[7, 8, 10, 19, 55, 69, 103, 104]
tuner id=65537 count=76 first_ids=[11, 14, 15, 16, 17, 18, 58, 63]
tuner id=65538 count=32 first_ids=[34, 41, 42, 60, 61, 62, 99, 102]
tuner id=65539 count=3 first_ids=[59, 198, 199]
tuner id=131073 count=3 first_ids=[215, 216, 228]
```

`notify_param1=0` は EDCB 全体ステータスが通常状態であることを示す。さらに、KonomiTV が `sendGetRecFilePath()` を呼ぶ対象、つまり現在時刻の前後2時間に重なる録画可能予約は `check_candidates=0` だった。

したがって、2026-06-27 21:45:23 JST 時点では、KonomiTV が録画中判定に失敗している状態は再現できなかった。この時点では EDCB 自体も録画中を示していない。

KonomiTV のログでは、21:36 頃に完了済み録画ファイルのサムネイル生成が完了し、21:39 頃にチャンネル・番組更新が走っていた。録画中判定に関係する明確な例外ログは確認できなかった。

## linux_reserve_change.md との関係

直接の通信経路は未変更なので、`sendGetRecFilePath()` コマンドそのものを壊した可能性は低い。

一方で、今回の `SyncResAutoAdd*` 実装は予約 ID に間接影響を与える。特に現在の設定が次の状態なら影響が出やすい。

```ini
SyncResAutoAddChange=1
SyncResAutoAddDelete=1
SyncResAutoAddChgNewRes=1
SyncResAutoAddChgKeepRecTag=0
```

`SyncResAutoAddChgNewRes=1` では、自動予約登録変更時に旧条件側の未来予約を削除し、新条件で予約を作り直す。これにより、番組自体は同じでも `reserve_id` が変わることがある。

KonomiTV の録画中判定は `reserve_id` を指定して `sendGetRecFilePath(reserve_id)` を呼ぶため、次のような状況では録画中判定が外れる可能性がある。

- KonomiTV の画面や内部状態が古い `reserve_id` を保持している
- 自動予約変更により、録画対象番組の予約が削除・再作成されて `reserve_id` が変わった
- 録画開始直前または開始直後に予約 ID が変わり、EDCB 側の `TunerBankCtrl` にある録画中予約 ID と KonomiTV が問い合わせる ID が一致しない
- 録画は始まっているが、EDCB 側の `recFilePath` がまだ埋まっておらず、`NWPLAY_TF_OPEN` が失敗する

さらに、現在の `CreateAutoAddSyncPgUID()` は `Create64PgKey()` と開始時刻上位ビットを加算しているため、照合キーとしては安全ではない。誤一致または誤除外が起きると、想定外の予約削除・変更につながり、KonomiTV 側には予約 ID の変化や予約欠落として見える。

## 切り分け方法

実際に録画中の時間帯に、KonomiTV と同じ経路で次を確認する。

```sh
cd ~/git/KonomiTV/server
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$PWD" .venv/bin/python - <<'PY'
import asyncio
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo
from pydantic_core import Url
from app.utils.edcb.CtrlCmdUtil import CtrlCmdUtil

JST = ZoneInfo('Asia/Tokyo')

def norm(dt):
    return dt.replace(tzinfo=JST) if dt.tzinfo is None else dt.astimezone(JST)

async def main():
    edcb = CtrlCmdUtil(Url('tcp://127.0.0.1:4510/'))
    now = datetime.now(JST)
    notify = await edcb.sendGetNotifySrvStatus()
    reserves = await edcb.sendEnumReserve()
    tuners = await edcb.sendEnumTunerReserve()
    print('now=', now.isoformat(timespec='seconds'))
    print('notify=', notify)
    print('reserve_count=', None if reserves is None else len(reserves))
    print('tuner_count=', None if tuners is None else len(tuners))
    if reserves is None:
        return
    for r in reserves:
        rec_mode = r.get('rec_setting', {}).get('rec_mode', 1)
        start = norm(r['start_time'])
        end = start + timedelta(seconds=r['duration_second'])
        if rec_mode < 5 and rec_mode != 4 and start <= now + timedelta(hours=2) and end >= now - timedelta(hours=2):
            path = await edcb.sendGetRecFilePath(r['reserve_id'])
            print(r['reserve_id'], start.isoformat(timespec='seconds'), end.isoformat(timespec='seconds'), path, r.get('title', ''))

asyncio.run(main())
PY
```

見るべき結果は次。

- `notify['param1'] == 1`
  - EDCB 全体として録画中
- 現在録画中の予約が `sendEnumReserve()` に存在する
- その予約 ID に対する `sendGetRecFilePath(reserve_id)` がパス文字列を返す

もし `notify['param1'] == 1` なのに、現在録画中の予約 ID で `sendGetRecFilePath()` が `None` になるなら、KonomiTV の現在の判定方法では録画中を取りこぼす。

もし `notify['param1'] == 0` なら、KonomiTV 以前に EDCB が録画中ステータスを出していない。

## EDCB 側の対策案

### 1. 変更後条件にも残る予約は削除・再作成しない

`SyncResAutoAddChgNewRes=1` でも、旧条件に一致し、かつ変更後条件にも一致する予約は削除せず、`ChgReserveData()` で録画設定だけ変更する。

これにより、例えば「全放送局・batなし」から「1放送局・batあり」へ変更した場合、変更後にも残る1件は同じ `reserve_id` のまま bat を追加し、旧条件だけに属する3件だけを削除できる。

KonomiTV にとっては、録画対象として残る番組の `reserve_id` が維持されるため、録画中判定の安定性が上がる。

### 2. 録画開始直前・録画中予約は削除・再作成しない

現在実装でも開始1分以内の予約は削除しないが、より保守的にするなら、予約時間が現在時刻と重なるもの、または録画中ファイルパスが取れるものは常に変更扱いにする。

`sendGetRecFilePath()` は `reserve_id` とチューナーバンク状態に依存するため、録画中や開始直前の ID 入れ替えは避けるべき。

### 3. EPG 照合キーを複合キーにする

`CreateAutoAddSyncPgUID()` の加算キーをやめ、次の2要素で照合する。

- `Create64PgKey(onid, tsid, sid, eid)`
- `ConvertI64Time(startTime)`

`std::pair<ULONGLONG, LONGLONG>` などで保持すれば、開始時刻ビットの加算による誤一致リスクを消せる。

### 4. 同期処理ログを追加する

`SaveDebugLog=1` のときだけでよいので、`SyncChangeAutoAddReserveData()` に次を出す。

- 対象自動予約 ID
- 旧条件一致予約数
- 変更後条件にも残る予約数
- 削除予約 ID
- 変更予約 ID
- 処理時間

予約 ID の入れ替わりが KonomiTV の不具合と一致するかを後から追えるようにする。

## KonomiTV 側の対策案

KonomiTV 側も、`sendGetRecFilePath()` だけに依存しないフォールバックを入れると堅くなる。

候補は次。

1. まず従来どおり `sendGetRecFilePath(reserve_id)` を呼ぶ
2. パス文字列が返れば録画中
3. 返らない場合でも、`sendGetNotifySrvStatus()` の `param1 == 1` で EDCB 全体が録画中なら追加判定する
4. `sendEnumTunerReserve()` の `reserve_list` に対象 `reserve_id` が含まれ、かつ現在時刻がその予約の録画時間帯に重なる場合は録画中候補として扱う

`sendEnumTunerReserve()` は未来の割当予約も返すため、これ単独で録画中判定に使ってはいけない。必ず現在時刻との重なり判定と組み合わせる必要がある。

実装場所は次。

- `ReservationsRouter.py`
  - `ReservationsAPI()` で `sendGetNotifySrvStatus()` と `sendEnumTunerReserve()` を1回ずつ取得
  - `GetIsRecordingInProgress()` に渡してフォールバック判定
- `ProgramsRouter.py`
  - 番組表側も同じヘルパーを使う

これにより、EDCB 側で `recFilePath` の取得が遅れる開始直後や、予約 ID 入れ替え直後の表示ブレを減らせる。

## 暫定回避策

KonomiTV の録画中判定が予約 ID の入れ替わりで壊れているか切り分けるだけなら、一時的に次を落とす。

```ini
SyncResAutoAddChgNewRes=0
```

これで自動予約変更時の削除・再作成が止まるため、予約 ID の変化が減る。ただし、旧条件から外れた予約を削除して作り直す動作も止まるので、ユーザーが期待している「3件削除、残る1件に bat 追加」の動作とは一致しなくなる。

より広く切り分けるなら次。

```ini
SyncResAutoAddChange=0
```

この場合は今回追加した自動予約変更時の予約同期処理全体が止まる。反映には設定再読み込みまたは EDCB 再起動が必要。録画中は再起動しない。

## 優先順位

まず安全に直すなら、次の順。

1. EDCB 側で、変更後条件にも残る予約を削除・再作成しないようにする
2. `CreateAutoAddSyncPgUID()` を複合キー化する
3. 録画開始直前・録画中予約を削除・再作成対象から明示的に外す
4. `SyncChangeAutoAddReserveData()` にデバッグログを追加する
5. KonomiTV 側に `sendGetRecFilePath()` 失敗時のフォールバック判定を追加する

## 現時点の結論

2026-06-27 21:45:23 JST の実測では、KonomiTV と同じ CtrlCmd API は EDCB に接続でき、予約一覧・チューナー予約一覧も取得できた。この時点では EDCB 全体ステータスが通常状態で、KonomiTV が録画中判定を行う候補予約も0件だったため、録画中判定の不具合は再現していない。

`linux_reserve_change.md` の変更が KonomiTV の判定コマンドを直接壊した可能性は低い。ただし、`SyncResAutoAddChgNewRes=1` による予約削除・再作成で `reserve_id` が変わることは、KonomiTV の `sendGetRecFilePath(reserve_id)` 依存の判定と相性が悪い。

本命対策は、EDCB 側で「変更後条件にも残る予約は ID を維持して変更する」ように直すこと。KonomiTV 側には、`sendGetRecFilePath()` 失敗時に EDCB 全体ステータス、チューナー割当、現在時刻の重なりを使うフォールバックを追加すると、開始直後や ID 変化に強くなる。

## 追加追試: KonomiTV 録画ファイル監視・メタデータ解析

短時間録画だけではなく、60秒を超える録画中ファイルで `RecordedScanTask` の挙動を確認した。

試験条件は次。

- 日時: 2026-06-27 22:07:55 JST から約150秒
- 放送局: NHK総合1・東京
- 予約名: `CodexRecordedScanTest_20260627_220640`
- EDCB 予約 ID: `230`
- 録画先: `/mnt/recording`
- 録画ファイル: `/mnt/recording/20260627_CodexRecordedScanTest_20260627_220640.ts`
- 録画プラグイン: `Write_Default.so`

録画中の EDCB 側状態は正常だった。

- `sendGetNotifySrvStatus()` 相当の状態で `notify_param1=1`
- `sendGetRecFilePath(230)` 相当の呼び出しで録画中ファイルパスを取得できた
- ファイルサイズは録画中に継続して増加した

確認できたサイズ変化は次。

```text
22:08:01  11,550,720 bytes
22:08:11  30,801,920 bytes
22:08:21  50,823,168 bytes
22:09:11 150,159,360 bytes
22:10:21 287,997,952 bytes
録画終了後 296,683,928 bytes
```

一方、KonomiTV 側では録画中に次のログが繰り返された。

```text
20260627_CodexRecordedScanTest_20260627_220640.ts: File is not recording. ignored.
```

このログは同一ファイルで386回出ていた。録画中に `File size changed.` は出ているが、`Saved metadata to DB. (status: Recording)` は出ず、`/api/videos` にも録画中ファイルは含まれなかった。

録画終了後は次の流れで正常に保存された。

```text
Recording or copying has just completed or has already completed.
Saved metadata to DB. (status: Recorded)
```

その後、録画ファイル削除後に KonomiTV も DB レコードを削除し、`/api/videos` から対象ファイルは消えた。

## 追加追試から見た原因

今回の症状は、EDCB の `sendGetRecFilePath()` が録画中ファイルを返せない問題ではなかった。EDCB 側の録画中状態、予約 ID、録画ファイルパス、ファイルサイズ増加はいずれも確認できた。

問題は KonomiTV の `server/app/tasks/RecordedScanTask.py` 側にある可能性が高い。

該当する流れは次。

1. `__handleFileChange()` が録画中ファイルのサイズ変化を検知する
2. 同じ関数内で `recording_info.file_size` を現在サイズへ更新する
3. その直後に `processRecordedFile()` を呼ぶ
4. `processRecordedFile()` は更新済みの `recording_info.file_size` を前回サイズとして読む
5. 結果として `file_size == last_size` になり、`mtime_continuous_start_at is None` のまま `File is not recording. ignored.` で戻る

つまり、呼び出し元では「サイズが変わった」と判定しているのに、解析関数に入る直前に前回サイズを現在サイズへ上書きしてしまうため、解析関数からは「サイズが変わっていない」ように見える。

このため、録画中ファイルが正常に伸び続けていても、録画中メタデータが保存されず、録画終了後にだけ `Recorded` として登録される。

## KonomiTV 側の本命対策案

`RecordedScanTask.__handleFileChange()` で、既知の録画中ファイルを処理する順序を変える。

方針は次。

1. サイズ変化を検知した直後に、先に `recording_info.file_size` を現在サイズへ上書きしない
2. `processRecordedFile()` には前回サイズが残った状態で入る
3. `processRecordedFile()` が `file_size != last_size` を見て録画中ファイルとして解析できるようにする
4. 解析後にだけ、必要なら `recording_info.file_size` と `mtime_continuous_start_at` を更新する
5. `processRecordedFile()` が `_recording_files[file_path]` を新しい `FileRecordingInfo` に置き換えた場合は、古い参照で上書きしない

あわせて、`UPDATE_THROTTLE_SECONDS` の扱いも整理する。

- 前回解析から30秒未満なら、状態だけ更新して重いメタデータ解析は行わない
- 30秒以上経過していれば、前回サイズを残したまま `processRecordedFile()` を呼ぶ
- これによりログスパムと解析負荷を抑えつつ、録画中メタデータ登録は行える

`File is not recording. ignored.` は警告ではなく、完了済み・停滞中ファイルのデバッグログに落とすほうがよい。録画中に正常なサイズ変化を検知しているケースで警告として大量出力されると、実際の障害ログを埋めてしまう。

## 修正後の確認項目

KonomiTV 側を修正した後、同じ条件で再試験する。

期待する結果は次。

1. NHK総合1・東京を `/mnt/recording` に60秒超録画する
2. 録画中に `File is not recording. ignored.` が連続しない
3. 録画中に `This file is recording or copying.` 相当のログが出る
4. 録画中に `Saved metadata to DB. (status: Recording)` が出る
5. 録画中に `/api/videos` が対象ファイルを返す
6. 録画終了後に同じレコードが `Recorded` 相当へ更新される
7. 試験録画ファイルを削除すると、KonomiTV からも対象レコードが消える

## 追加追試後の後片付け

録画ファイル本体と EDCB の付随ファイルは削除済み。

- `/mnt/recording/20260627_CodexRecordedScanTest_20260627_220640.ts`
- `/mnt/recording/20260627_CodexRecordedScanTest_20260627_220640.ts.program.txt`
- `/mnt/recording/20260627_CodexRecordedScanTest_20260627_220640.ts.err`

`/mnt/recording` には `*CodexRecordedScanTest_20260627_220640*` に一致するファイルが残っていないことを確認した。KonomiTV の `/api/videos` からも対象パスは消えている。

ただし、KonomiTV が生成したサムネイルは `nobody:nogroup` 所有のため、現在の権限では削除できなかった。

- `/home/oomugi413/git/KonomiTV/server/data/thumbnails/b7a1cfd9f9d98218dd5e102edbcd783b_tile.webp`
- `/home/oomugi413/git/KonomiTV/server/data/thumbnails/b7a1cfd9f9d98218dd5e102edbcd783b.webp`

`sudo -n rm` では `sudo: interactive authentication is required` になった。録画ファイルそのものは削除済みなので、ユーザーの指定した `/mnt/recording` の試験録画ファイルは残っていない。

## 追加追試後の結論

KonomiTV の「録画中かどうか」判定には2系統ある。

- 予約一覧・番組表の録画中判定: EDCB の `sendGetRecFilePath(reserve_id)` に依存
- 録画ファイル監視・メタデータ解析: KonomiTV の `RecordedScanTask` がファイルサイズと mtime を監視

今回の 150 秒録画では、前者は正常だった。後者は録画中ファイルのサイズ増加を検知しているにもかかわらず、解析直前の状態更新順序により `File is not recording. ignored.` へ落ちていた。

したがって、このエラーの解消には EDCB 側よりも KonomiTV 側の `RecordedScanTask` 修正が必要。`linux_reserve_change.md` の変更で発生し得る予約 ID 入れ替わり問題とは別に、KonomiTV の録画中メタデータ解析ロジック自体を直す必要がある。
