# Linux で SyncResAutoAdd* を使えるようにする提案

## 背景

対象の 4 オプションは現在 EpgTimer 側の `Settings` にだけ存在し、処理も WPF クライアントの `MenuUtil` に閉じている。

- `SyncResAutoAddChange`: 自動予約登録の変更時、既存予約の録画設定も変更後の自動予約登録に合わせる。
- `SyncResAutoAddDelete`: 自動予約登録の削除時、その自動予約登録だけに紐づく自動追加予約を削除する。
- `SyncResAutoAddChgNewRes`: 変更時に既存予約を変更せず、旧条件だけに紐づく予約を削除して新条件の予約を作り直す。
- `SyncResAutoAddChgKeepRecTag`: 変更時、予約側の録画タグを維持する。

Windows 版では `EpgTimer/EpgTimer/Menu/MenuUtil.cs` の `AutoAddChange()` / `AutoAddDelete()` が、自動予約登録コマンドを送る前後で `ReserveDelete()` / `ReserveChange()` を追加実行している。Linux の WebUI / Lua API は `EpgTimerSrv` へ直接 `AddOrChgAutoAdd()`、`DelAutoAdd()`、`AddOrChgManuAdd()`、`DelManuAdd()` を呼ぶため、このクライアント側処理を通らない。

## 推奨方針

Linux では同期処理を `EpgTimerSrv` 側へ移す。WebUI だけに Lua 実装を足す案も可能だが、HTTP API、Lua、CtrlCmd の経路で挙動が分かれるため、サーバー側の共通処理にする方が安全。

実装先の中心は以下。

- `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.h`
- `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.cpp`
- `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.h`
- `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp`
- `HttpPublic/EMWUI/*` と必要なら `ini/HttpPublic/legacy/*`

## 設定の持ち方

`CEpgTimerSrvSetting::SETTING` に次を追加し、`EpgTimerSrv.ini` の `[SET]` から読む。

```cpp
bool syncResAutoAddChange;
bool syncResAutoAddDelete;
bool syncResAutoAddChgNewRes;
bool syncResAutoAddChgKeepRecTag;
```

キー名は Windows 側と同じ `SyncResAutoAddChange` などにする。既定値はすべて `false`。これにより既存環境では挙動を変えない。

WebUI には設定画面を追加する。最初は `EpgTimerSrv.ini` を直接編集して `ReloadSetting` で反映できる形でもよいが、「Linux でも使える」と言える状態にするなら EMWUI の設定画面にチェックボックスを置くのが望ましい。

## サーバー側に追加する共通処理

`CEpgTimerSrvMain` に次のような内部処理を追加する。

- 変更前の自動予約登録から、現在紐づいている予約一覧を作る。
- 変更後の自動予約登録の録画設定を使って `chgReserveList` を作る。
- `SyncResAutoAddChgNewRes` が有効なら、旧条件だけに紐づく予約を `delReserveList` に回す。
- 自動予約登録の削除時は、削除対象だけに紐づく自動追加予約の ID を作る。
- 予約変更時は `PreChgReserveData()` を通して既存の予約変更経路に合わせる。

候補名の例:

```cpp
void BuildAutoAddSyncChangeList(...);
void BuildAutoAddSyncDeleteList(...);
bool ApplyAutoAddSyncForChange(...);
bool ApplyAutoAddSyncForDelete(...);
```

呼び出し順は Windows 側に合わせる。

1. 自動予約登録を変更する前に、変更前条件で同期対象予約を計算する。
2. 必要なら予約削除、予約変更を先に実行する。
3. 自動予約登録を変更する。
4. 既存の `AutoAddReserveEPG()` / `AutoAddReserveProgram()` で新規予約を追加する。

削除時は、削除前に対象予約を計算し、自動予約登録を削除した後に予約を削除する。

## 紐づき判定

### 共通

対象予約は Windows 側と同じく「自動予約由来」の予約だけに絞る。

```cpp
!reserve.comment.empty() && reserve.comment.back() != L'$'
```

`SeparateFixedTuners` が有効なときは、自動予約登録側の `recSetting.tunerID` と予約側の `recSetting.tunerID` が一致するものだけを同一グループとして扱う。変更処理では「変更前の TunerID」を使う。

### EPG 自動予約

変更前の `EPG_AUTO_ADD_DATA.searchInfo` で `epgDB.SearchEpg()` を実行し、検索結果のイベントと予約を突き合わせる。Windows 側の `SearchItem.SetReserveData()` は `CurrentPgUID()` を使っているため、C++ 側でも単純な `Create64PgKey()` だけではなく開始日を含めたキーを使う。

例:

```cpp
struct PgMatchKey {
    ULONGLONG pgKey;      // Create64PgKey(onid, tsid, sid, eid)
    ULONGLONG startBucket; // (ULONGLONG)ConvertI64Time(startTime) & 0xFFFFFF0000000000ULL
};
```

`^!{999}` で無効化された EPG 自動予約も同期対象検索では無効フラグを外して検索する。これは WPF 側が `keyDisabledFlag = 0` にして検索しているため。

### プログラム自動予約

予約一覧だけで判定できる。

- `reserve.eventID == 0xFFFF`
- ONID/TSID/SID が一致
- 開始曜日と開始秒が `dayOfWeekFlag` / `startTime` に合う
- `durationSecond` が一致

無効化されたプログラム自動予約は `dayOfWeekFlag == 0` かつタイトルに `^!{999}[xx]` が入る場合があるため、ヘルパーで実効曜日を復元して使う。

## オプション別の動作

`SyncResAutoAddChange`

- 自動予約登録変更時だけ有効。
- 旧条件に紐づく自動追加予約の `recSetting` を、変更後の自動予約登録の `recSetting` に置き換える。
- 既存予約が無効予約なら、変更後も無効状態を保持する。
- プログラム自動予約では、予約タイトルも変更後の自動予約登録タイトルへ合わせる。

`SyncResAutoAddChgKeepRecTag`

- `SyncResAutoAddChange=true` かつ `SyncResAutoAddChgNewRes=false` のときだけ効かせる。
- C++ 側では録画タグは `REC_SETTING_DATA::batFilePath` の `*` 以降にパックされるため、変更前予約の `*tag` 部分を変更後設定へ戻す。
- 変更前予約にタグが無ければ、変更後自動予約登録側のタグも消す。

`SyncResAutoAddChgNewRes`

- 旧条件だけに紐づく予約を削除対象にし、変更後自動予約登録で新規予約を作らせる。
- ただし録画中、または直近開始の予約は削除せず、通常の変更対象に残す。Windows 側は `CautionOnRecMarginMin` を使っているが、サーバー設定へは持ち込まれていないため、まずは 1 分以内を保護するのが無難。厳密に合わせるなら `SyncResAutoAddCautionMarginMin` 相当のサーバー設定を追加する。

`SyncResAutoAddDelete`

- 自動予約登録削除時だけ有効。
- 削除対象以外の有効な EPG/プログラム自動予約にも紐づく予約は削除しない。
- 個別予約や `$` で自動予約から切り離された予約は削除しない。

## 呼び出し箇所

最初に対応すべき経路:

- `LuaAddOrChgAutoAdd()`
- `LuaAddOrChgManuAdd()`
- `LuaDelAutoAdd()`
- `LuaDelManuAdd()`
- `CMD2_EPG_SRV_CHG_AUTO_ADD`
- `CMD2_EPG_SRV_CHG_MANU_ADD`
- `CMD2_EPG_SRV_DEL_AUTO_ADD`
- `CMD2_EPG_SRV_DEL_MANU_ADD`

`CMD2_*_ADD_*` は新規追加なので既存予約同期は不要。旧互換コマンドは、上記の内部共通関数に寄せれば自然に対応できる。

背景の自動予約チェック処理には入れない。これはユーザー操作で自動予約登録を変更・削除したときの補助機能であり、周期処理に入れると意図しない予約変更や削除につながる。

## WebUI 対応

EMWUI の自動予約変更は既に `/api/SetAutoAdd` と `/api/SetManuAdd` を通るため、Lua 関数側でサーバー同期が動けばフォーム側の大きな変更は不要。

削除は現在 `autoaddepg.html` / `autoaddmanual.html` から直接 `edcb.DelAutoAdd()` / `edcb.DelManuAdd()` を呼んでいる。これらの Lua 関数にも同期処理を入れれば UI 側の削除ボタンもそのまま対応できる。

設定 UI は次を追加する。

- `自動予約登録の変更時に予約も合わせる`
- `条件変更時は予約を作り直す`
- `予約側の録画タグを保持する`
- `自動予約登録の削除時に予約も削除する`

## テスト観点

最低限、次を Linux の WebUI/API 経由で確認する。

1. EPG 自動予約の録画モードや録画フォルダを変更すると、既存の自動追加予約だけが変わる。
2. 同じ予約に複数の有効な自動予約登録が当たる場合、削除しすぎない。
3. `SeparateFixedTuners=1` で TunerID が違う予約を巻き込まない。
4. `SyncResAutoAddChgNewRes=1` で旧条件だけの未来予約が削除され、新条件の予約が追加される。
5. 録画中または直近開始の予約は削除されない。
6. `SyncResAutoAddChgKeepRecTag=1` で予約側の `batFilePath` の `*tag` が残る。
7. プログラム自動予約の変更時、予約タイトルと録画設定が追従する。
8. 自動予約から切り離された `$` 付き予約や個別予約は削除・変更されない。

## 段階的な実装案

1. `EpgTimerSrv.ini` の 4 設定を読み込むだけの変更を入れる。
2. `CEpgTimerSrvMain` に同期対象の列挙ヘルパーを追加し、単体でログ確認できるようにする。
3. Lua の `AddOrChg*` / `Del*` から同期処理を呼ぶ。
4. CMD2 の変更・削除コマンドも同じ内部関数へ寄せる。
5. EMWUI の設定画面を追加する。
6. 必要なら legacy WebUI にも同じ設定項目を追加する。
