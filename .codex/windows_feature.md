# Windows版由来でLinux版に未反映・挙動差のある録画関連機能

## 調査範囲と判定基準

- 調査対象リビジョン: `527eaf1c` (`master`、2026-07-06)
- 比較対象:
  - Windowsクライアント: `EpgTimer/`
  - Linux版のサーバー、ライブラリ、設定: `/usr/local/lib/edcb/`、`/var/local/edcb/`
  - Legacy Web UI: `/var/local/edcb/HttpPublic/legacy/`
  - Material WebUI: `/home/oomugi413/git/EDCB_Material_WebUI/`
- 録画予約の内容、録画開始準備、録画後処理に影響する機能だけを対象とした。
- 表示だけの機能、Windowsクライアント内で完結する操作、Material WebUIに同等の導線がある機能は除外した。
- 「未反映」には、Linuxサーバーに基本機能はあるものの、Windows版と録画に関わる動作が異なる場合も含める。

## 該当機能

### 1. 自動予約登録の変更・削除に予約を連動させる際の録画直前保護の差

- 元の機能: `bc477fd1a9edcd57313d208c65e3eef28beeadb9`（2016-01-30、自動予約登録に合わせて予約も変更・削除するオプションを追加ほか）
- Windows側の保護処理: `6c5fd4de0c1514360771f382d0f55d037f040906`（2016-02-01、録画中の予約を削除しないよう変更）
- Linux側の連動処理: `819150664021e67f9e48cd54198a04e06aa5b2c1`（2026-06-27、Linux版の自動削除対応）
- Linuxの `EpgTimerSrvMain.cpp` には `SyncResAutoAddChange`、`SyncResAutoAddDelete`、`SyncResAutoAddChgNewRes`、`SyncResAutoAddChgKeepRecTag` が実装され、`/var/local/edcb/EpgTimerSrv.ini` にも設定がある。
- ただし、Windowsの `MenuUtil.AutoAddSyncChangeList` が `CautionOnRecMarginMin`（既定5分）を使うのに対し、Linuxの `SyncChangeAutoAddReserveData` は保護時間を60秒に固定している。Linux版にはこの余裕時間に対応する設定がない。
- 予約削除・再登録のタイミングが異なり、録画中または録画直前の予約変更時に録画を継続できるかが変わるため、単なる表示差ではない。

### 2. プログラム予約をEPG予約へ戻す操作

- コミット: `e16ba539c9442e361ea8a8a384142d5e814e6035`（2015-03-17、プログラム予約をEPG予約に変更可能にした）
- Windowsの `MenuUtil.ReserveChangeResMode` は、プログラム予約の `EventID` を該当EPGへ戻す逆変換を行う。反対方向のEPG予約化も同じ操作から行える。
- Legacy Web UI (`reserveinfo.html`) と Material WebUI (`reserveinfo.html` / `util.lua`) は「プログラム予約化」だけで、EPG予約へ戻す操作を提供していない。Material側にも「元に戻せません」と明記されている。
- EPG予約に戻せないため、番組イベントの変更、延長追従、ぴったり録画など、その後の録画動作を復元できない。

### 3. 個別予約を自動予約登録の連動対象から外す操作

- コミット: `6b669dd337bfaec795d74d0d4748b163f191f0b9`（2016-05-15、自動登録予約を解除するチェックボックスを追加）
- Windowsの予約変更画面 (`ChgReserveWindow.xaml`) に「自動予約登録を解除する」があり、`ReleaseAutoAdd()` によって個別予約へ変更し、以後の自動予約登録による変更・削除の対象から外す。
- Legacy Web UI と Material WebUI の予約変更画面には、この解除操作に相当するチェックボックスやAPI操作がない。
- 解除できないと、後続の自動予約登録の変更・削除で対象予約の録画設定や予約自体が変更される可能性がある。

### 4. 既存予約を自動予約登録の録画設定へ一括で合わせる操作

- コミット: `6e81338948941b6d90a0e059c727c51166e6f904`（2015-09-13、予約の録画設定を自動登録の設定に合わせるメニューコマンドを追加）
- Windowsの `MenuUtil.AutoAddChangeSyncReserve` は、既存の一致予約を一括して自動予約登録の録画設定へ更新する。手動で録画設定を変更した予約も対象にできる。
- Legacy Web UI と Material WebUI には、この一括同期を実行するボタン、APIエンドポイント、相当する一連の操作がない（個別予約や自動予約登録の編集は可能）。
- 録画モード、保存先、録画後BAT、チューナー等がまとめて変わるため、録画動作に直接影響する。

### 5. 番組表タブごとの録画プリセット

- コミット: `504e93e810177ba34f7f491f423657c8f5c5ed9f`（2019-08-04、番組表ごとに録画プリセットを設定可能にした）
- Windowsの `CustomEpgTabInfo.RecSetting` にタブごとの録画設定を保存し、`EpgViewBase` がその設定を新規予約へ渡す。
- Material WebUIの `epgcustom.html` は検索条件のプリセットを扱うだけで、タブ単位の録画設定を保存・予約へ自動適用する機能はない。Legacy Web UIにもない。
- 同じ番組を予約しても、どのWindows番組表タブから追加したかで録画設定が変わるため、Linux WebUIでは予約作成時の録画動作を再現できない。

### 6. 予約録画開始前の録画先HDD起床

- コミット: `ae75009807a41c852fd6d349a45c618d5477efba`（2017-05-29、予約録画開始準備のタイミングで録画先フォルダへアクセスする機能を追加）
- Windowsの `CommonManagerClass.WakeUpHDDWork` は、録画開始前に録画先へ一時フォルダーを作成・削除して、停止中のHDDを起こす。`WakeUpHdd`、`NoWakeUpHddMin`、`WakeUpHddOverlapNum` はWindows EpgTimer側で扱われる。
- `EpgTimerSrv` のLinux実装、Legacy Web UI、Material WebUIには同等の録画前アクセス処理や設定がない。
- チューナー予約の選択そのものではないが、録画開始時のディスク起動遅延や開始安定性に影響する。

### 7. 録画プリセット変更時に録画タグを保持する設定

- コミット: `baed76e5cf4ba85e1c08f61d77e0d5a19b8a5ff6`（2020-04-16、録画プリセット変更時に録画タグを保持するオプションを追加）
- Windowsの `SettingClass.SetWithoutRecTag` と `RecSettingView.xaml` の「録画タグを除く」により、プリセットを読み込んでも既存予約の `RecTag` を維持できる。
- Material WebUIは録画タグ自体の編集はできるが、プリセット適用時にタグだけ保持する設定や操作はない。Legacy Web UIにもない。
- `RecTag` は録画後BATやファイル名マクロに使われるため、録画後処理・出力名が変わり得る。ただし録画開始時刻やチューナー選択への影響はない。

## 今回の整理で除外したもの

旧版に記載していた右クリックメニュー、横断検索、F5強制更新、`NoSendClose`、録画後BATのウィンドウ表示、Andキーワード検索、再生パス置換、番組表の表示結合、`.program.txt` 保存ダイアログの挙動は、表示・検索・再生などのクライアント内機能であるか、Material WebUIにも類似機能があるため、この一覧から外した。

## 注意

ここでいう「未反映」は、LinuxサーバーのAPIで個別に代替操作できないという意味ではない。Windows版にある操作、設定、または動作のまとまりが、Linux版のサーバー・Legacy Web UI・Material WebUIで同等には提供されていないことを指す。
