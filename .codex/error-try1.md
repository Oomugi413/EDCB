# EDCB API エラー確認と対策案 1

## 前提

対象は `linux_reserve_change.md` で追加した `SyncResAutoAdd*` 関連処理。

録画中環境のため、`systemctl start` / `systemctl stop` / `systemctl restart` は実行していない。確認は、実行中の `EpgTimerSrv` への読み取りAPIと、一致0件になる一時EPG自動予約の追加・変更・削除APIだけで行った。

## 現在の設定

`/var/local/edcb/EpgTimerSrv.ini` では、今回追加した設定がすべて有効になっていた。

```ini
SyncResAutoAddChange=1
SyncResAutoAddDelete=1
SyncResAutoAddChgNewRes=1
SyncResAutoAddChgKeepRecTag=0
```

`EpgTimerSrv` は pid `2451905` で起動しており、HTTP API は `0.0.0.0:5510`、CtrlCmd系TCPは `0.0.0.0:4510` で待ち受けていた。

## 実施したAPI試験

### 読み取りAPI

以下はすべて HTTP 200 で成功した。

```sh
curl -sS -m 20 -w '\nHTTP_CODE=%{http_code}\nTIME_TOTAL=%{time_total}\nSIZE=%{size_download}\n' \
  'http://127.0.0.1:5510/api/EnumAutoAdd?json=1' \
  -o /tmp/edcb-api-enumautoadd.json

curl -sS -m 20 -w '\nHTTP_CODE=%{http_code}\nTIME_TOTAL=%{time_total}\nSIZE=%{size_download}\n' \
  'http://127.0.0.1:5510/api/EnumReserveInfo?json=1' \
  -o /tmp/edcb-api-enumreserve.json

curl -sS -m 20 -w '\nHTTP_CODE=%{http_code}\nTIME_TOTAL=%{time_total}\nSIZE=%{size_download}\n' \
  'http://127.0.0.1:5510/api/EnumManuAdd?json=1' \
  -o /tmp/edcb-api-enummanuadd.json
```

結果。

- `EnumAutoAdd`: HTTP 200、約 0.011 秒、34件
- `EnumReserveInfo`: HTTP 200、約 0.012 秒、137件
- `EnumManuAdd`: HTTP 200、約 0.004 秒、1件

このため、少なくとも一覧取得系APIの常時エラーではない。

### POST API と CSRF

`SetAutoAdd` / `SetManuAdd` に `ctok` なし、または別画面用の `ctok` でPOSTすると、`curl` では次のようになった。

```txt
curl: (1) Received HTTP/0.9 when not allowed
HTTP_CODE=000
```

一方、正しい `ctok` を付けて状態を変更しないPOSTを行うと、HTTP 200 で通常のJSONエラーになった。

```json
{"err":"不正値入力"}
```

該当箇所は `/var/local/edcb/HttpPublic/api/util.lua` の `AssertPost()` / `AssertCsrf()`。POST時に `ctok` が不正だと `assert()` で落ち、HTTPレスポンスが壊れたように見える。

外部クライアントからAPIを直接叩いている場合、`SetAutoAdd` / `SetManuAdd` / `SetReserve` などの変更系APIには、対象API用の正しい `ctok` をフォームから取得して付ける必要がある。

### 一時EPG自動予約での変更API試験

予約に一致しない検索語 `__codex_nohit_sync_test_20260627__` で一時EPG自動予約を追加した。

追加APIは成功した。

```json
{"success":"EPG自動予約を追加しました"}
```

追加後の一時自動予約は ID `36`、`addCount=0`。

その後、ID `36` を `__codex_nohit_sync_test_20260627_changed__` に変更した。これは `SyncChangeAutoAddReserveData()` を通る試験になる。

結果。

- HTTP 200
- 約 0.56 秒
- レスポンスは成功

```json
{"success":"EPG自動予約を変更しました"}
```

最後に ID `36` を削除した。

```json
{"success":"EPG自動予約を削除しました"}
```

削除後の `EnumAutoAdd` は34件に戻り、一時検索語の残存はなかった。

## ログ確認

`journalctl -u edcb --since '2026-06-27 21:20:00' --no-pager` では、API試験による `EpgTimerSrv` のエラー出力はなかった。

20時台に出ていた `Broken pipe`、`Conversion failed!`、AAC/MPEG2 decode warning は ffmpeg の視聴・配信系ログで、今回の `SyncResAutoAdd*` 変更処理とは直接関係しない可能性が高い。

## 観測した注意点

一時自動予約の変更は一致0件でも約0.56秒かかった。

理由は、変更時に `BuildEnabledAutoAddReserveMap()` が全有効自動予約を走査し、各EPG自動予約について `EnumEpgAutoAddReserveIDs()` から `epgDB.SearchEpg(&key, 1, 0, LLONG_MAX, ...)` を呼ぶため。

現在の自動予約は34件すべて有効で、サービス指定数の合計は1415件。上位にはサービス指定116件以上の自動予約が多数ある。

```txt
id=28 services=125 addCount=2 key=キム・ポッシブル
id=4  services=116 addCount=0 key=プロジェクトX
id=6  services=116 addCount=0 key=映像の世紀
id=10 services=116 addCount=7 key=水曜どうでしょう
id=20 services=116 addCount=8 key=T・クルーズ|M：I|ミッション：インポッシブル
```

したがって、実際に予約が多数一致する自動予約の変更・削除では、WebUIや外部APIクライアント側でタイムアウトまたは「APIエラー」と見える可能性がある。

また、`SyncResAutoAddChgNewRes=1` の現在実装では、同一内容の保存でも `HasEnabledAutoAddAfterRemove()` の判定上、対象自動予約だけに紐づく未来予約を削除して作り直す経路に入る可能性がある。これは予約IDの変更や処理時間増加を招く。

## 関連する実装箇所

`EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp`

- `CreateAutoAddSyncPgUID()` 周辺
  - 行166付近
  - `Create64PgKey()` に開始時刻上位ビットを加算しており、キー衝突・桁あふれ的な誤判定リスクがある
- `EnumEpgAutoAddReserveIDs()`
  - 行189付近
  - `SearchEpg(..., 0, LLONG_MAX, ...)` で広い範囲を検索する
- `BuildEnabledAutoAddReserveMap()`
  - 行247付近
  - 変更・削除時に全有効自動予約を再検索する
- `SyncChangeAutoAddReserveData()`
  - 行1614付近
  - 変更前条件に一致する予約を収集し、`SyncResAutoAddChgNewRes=1` のとき削除候補にする
- `SyncResAutoAddChgNewRes` の削除判定
  - 行1712付近
  - 変更後条件にも残る予約を明示的に除外していない

## すぐできる回避策

### 外部APIクライアント側

変更系APIを直接叩く場合、必ずWebUIフォームから対象API用の `ctok` を取得してPOSTに含める。

`ctok` なしのPOSTは、EDCBのLua API側でHTTP 403ではなく接続断に近い見え方になるため、クライアントでは「APIエラー」として見える。

### 設定側

自動予約変更時だけAPIエラーが出る場合は、暫定的に以下のどちらかで切り分ける。

```ini
SyncResAutoAddChange=0
```

または、削除・再作成の重さを避けるために以下だけ落とす。

```ini
SyncResAutoAddChgNewRes=0
```

反映には設定再読み込みまたはEDCB再起動が必要。録画中は再起動しない。

## コード側の対策案

### 1. EPG予約照合キーを安全な複合キーにする

`CreateAutoAddSyncPgUID()` は `Create64PgKey()` と開始時刻ビットを加算しているため、誤一致の余地がある。

対策として、`set<ULONGLONG>` ではなく、次のような複合キーを使う。

- `pgKey = Create64PgKey(onid, tsid, sid, eid)`
- `startTime = ConvertI64Time(startTime)`

この2要素を `std::pair<ULONGLONG, LONGLONG>` などで保持し、加算しない。

### 2. `SyncResAutoAddChgNewRes=1` の削除対象を「旧条件だけ」に限定する

変更前条件に一致する予約のうち、変更後条件にも一致する予約は削除せず、録画設定変更だけにする。

これにより、同一内容保存やサービス絞り込み後にも残る番組で、不要な削除・再作成を避けられる。

### 3. 全自動予約の再検索を避ける

`BuildEnabledAutoAddReserveMap()` は重い。少なくとも以下のどちらかに寄せる。

- 対象予約IDだけについて他自動予約との紐づきを判定する
- `reserve.comment` の自動予約コメントや既存の紐づき情報を優先し、必要時だけEPG検索する

全34件の有効自動予約を毎回 `SearchEpg()` しないようにするのが本命。

### 4. API失敗の見え方を改善する

`SyncChangeAutoAddReserveData()` の同期失敗を `AddOrChgAutoAdd()` 全体の失敗に直結させると、WebUIでは「EPG自動予約を変更できませんでした」になる。

同期処理はベストエフォートとして、同期予約変更の失敗時も自動予約登録自体は変更する設計にする案がある。ただし、途中まで予約削除した後に失敗すると不整合になるため、先に削除対象・変更対象を作成してから、失敗時の扱いを明確化する必要がある。

### 5. デバッグログを追加する

`SaveDebugLog=1` のときだけでよいので、以下を出す。

- `SyncChangeAutoAddReserveData()` 開始・終了
- 対象自動予約ID
- 変更前一致予約数
- 変更後にも残る予約数
- 削除予約数
- 変更予約数
- 処理時間

今回のようなAPIエラーでは、HTTP側だけでは原因が見えにくいため、同期処理の計測ログが必要。

## 優先順位

まず安全に直すなら、次の順。

1. `CreateAutoAddSyncPgUID()` を複合キー化する
2. `SyncResAutoAddChgNewRes=1` でも変更後条件に残る予約は削除しない
3. `BuildEnabledAutoAddReserveMap()` を対象予約ID中心の判定へ縮小する
4. 同期処理ログを追加する
5. 必要なら、同期失敗時のAPI失敗扱いを見直す

## 現時点の結論

読み取りAPIは正常。`ctok` なしまたは不正な変更系POSTは、HTTPレスポンスが壊れた形でエラーになる。

正しい `ctok` 付きの `SetAutoAdd` は成功するが、今回追加した同期変更経路は一致0件でも約0.56秒かかった。現在の自動予約条件は広く、実運用の自動予約変更・削除ではAPIタイムアウトやWebUI上のAPIエラーにつながる可能性が高い。

今回の変更に関連する本命対策は、同期処理のEPG検索範囲と削除対象を絞ること。
