# Linux で SyncResAutoAdd* を使えるようにした変更

## 背景

`SyncResAutoAddChange`、`SyncResAutoAddDelete`、`SyncResAutoAddChgNewRes`、`SyncResAutoAddChgKeepRecTag` は、Windows の WPF クライアント側処理に依存していた。Linux で WebUI / Lua API / CtrlCmd から直接 `EpgTimerSrv` を操作する場合、この処理を通らないため、サーバ側に同期処理を追加した。

## 実装範囲

変更した既存ファイルは以下。

- `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.h`
- `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.cpp`
- `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.h`
- `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp`

EMWUI の設定画面修正は、ユーザー指示により今回の作業から外した。

## 設定の持ち方

`CEpgTimerSrvSetting::SETTING` に次を追加した。

```cpp
bool syncResAutoAddChange;
bool syncResAutoAddDelete;
bool syncResAutoAddChgNewRes;
bool syncResAutoAddChgKeepRecTag;
```

`EpgTimerSrv.ini` の `[SET]` から次のキーを読む。既定値はすべて `false`。

- `SyncResAutoAddChange`
- `SyncResAutoAddDelete`
- `SyncResAutoAddChgNewRes`
- `SyncResAutoAddChgKeepRecTag`

設定UIは未追加のため、現時点では `EpgTimerSrv.ini` を編集し、`ReloadSetting` 経由で反映する。

## サーバー側に追加した共通処理

`CEpgTimerSrvMain` に以下を追加した。

- `SyncChangeAutoAddReserveData()`
- `SyncDeleteAutoAddReserveData()`

変更時は、自動予約登録を変更する前に旧条件で既存予約を列挙し、設定に応じて予約変更または予約削除を先に行う。その後、従来どおり自動予約登録を変更し、`AutoAddReserveEPG()` / `AutoAddReserveProgram()` で新条件の予約追加を行う。

削除時は、自動予約登録を削除する前に旧条件で対象予約を列挙し、削除対象以外の有効な自動予約登録にも紐づく予約を除外してから削除する。

## 紐づき判定

### 共通

同期対象は自動予約由来の予約だけに限定した。

```cpp
!reserve.comment.empty() && reserve.comment.back() != L'$'
```

`SeparateFixedTuners` が有効な場合は、自動予約登録側の `recSetting.tunerID` と予約側の `recSetting.tunerID` が一致するものだけを対象にした。

### EPG 自動予約

変更前の `EPG_AUTO_ADD_DATA.searchInfo` で `epgDB.SearchEpg()` を実行し、検索結果と予約を `Create64PgKey()` と開始時刻バケットで突き合わせる。

`^!{999}` で無効化された EPG 自動予約は、同期対象検索時だけ無効プレフィックスを外して検索する。

### プログラム自動予約

予約一覧から次の条件で一致判定する。

- `eventID == 0xFFFF`
- ONID / TSID / SID が一致
- 開始曜日、開始秒、録画時間が一致
- `SeparateFixedTuners` 有効時は TunerID も一致

無効化されたプログラム自動予約の `^!{999}[xx]` タイトル表現も、実効曜日と実効タイトルへ戻して扱う。

## オプション別の動作

`SyncResAutoAddChange`

- 自動予約登録変更時、旧条件に紐づく自動追加予約の `recSetting` を変更後の自動予約登録に合わせる。
- 既存予約が無効予約なら、変更後も無効状態を保持する。
- プログラム自動予約では、予約タイトルも変更後の自動予約登録タイトルに合わせる。

`SyncResAutoAddChgKeepRecTag`

- `SyncResAutoAddChange=true` かつ `SyncResAutoAddChgNewRes=false` のときに、予約側の `batFilePath` の `*` 以降を保持する。

`SyncResAutoAddChgNewRes`

- 旧条件だけに紐づく未来予約を削除し、変更後自動予約登録で新規予約を作り直す。
- 開始まで1分以内の予約は削除せず、通常の変更対象に残す。

`SyncResAutoAddDelete`

- 自動予約登録削除時、削除対象だけに紐づく自動追加予約を削除する。
- 個別予約、`$` で自動予約から切り離された予約、他の有効な自動予約登録にも紐づく予約は削除しない。

## 呼び出し箇所

同期処理を追加した経路は以下。

- `LuaAddOrChgAutoAdd()`
- `LuaAddOrChgManuAdd()`
- `LuaDelAutoAdd()`
- `LuaDelManuAdd()`
- `CMD2_EPG_SRV_CHG_AUTO_ADD`
- `CMD2_EPG_SRV_CHG_MANU_ADD`
- `CMD2_EPG_SRV_DEL_AUTO_ADD`
- `CMD2_EPG_SRV_DEL_MANU_ADD`
- `CMD2_EPG_SRV_CHG_AUTO_ADD2`
- `CMD2_EPG_SRV_CHG_MANU_ADD2`
- `CMD_EPG_SRV_CHG_AUTO_ADD`
- `CMD_EPG_SRV_DEL_AUTO_ADD`

新規追加コマンドと、周期的な背景自動予約チェック処理には同期処理を入れていない。

## WebUI 対応

EMWUI のフォーム修正は今回中止した。

ただし、WebUI が既存の Lua API `AddOrChgAutoAdd()` / `AddOrChgManuAdd()` / `DelAutoAdd()` / `DelManuAdd()` を通る場合は、サーバ側で同期処理が動く。

## テスト観点

今回実施した確認はビルドのみ。

```sh
make -C EpgTimerSrv/EpgTimerSrv
```

`systemctl` による EDCB 再起動、実サーバへの設定反映、録画中環境での実操作テストは行っていない。
