# Linux reserve codediff

## 変更ファイルと退避ファイル

以下の既存ファイルは、変更前の内容を `.linux_reserve_orig` へ退避し、元のファイル名で新しい変更版を作成した。

| 新ファイル | 変更前ファイル |
| --- | --- |
| `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.h` | `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.h.linux_reserve_orig` |
| `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.cpp` | `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.cpp.linux_reserve_orig` |
| `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.h` | `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.h.linux_reserve_orig` |
| `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp` | `EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp.linux_reserve_orig` |

新規作成した文書ファイルは以下。元ファイルは存在しなかったため退避ファイルはない。

- `.codex/linux_reserve_codediff.md`
- `.codex/linux_reserve_change.md`

## 復旧方法

元のコードへ戻す場合は、リポジトリルート `/home/oomugi413/git/EDCB` で次を実行する。

```sh
cp -p EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.h.linux_reserve_orig EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.h
cp -p EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.cpp.linux_reserve_orig EpgTimerSrv/EpgTimerSrv/EpgTimerSrvSetting.cpp
cp -p EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.h.linux_reserve_orig EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.h
cp -p EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp.linux_reserve_orig EpgTimerSrv/EpgTimerSrv/EpgTimerSrvMain.cpp
```

復旧後の確認は再起動なしで次だけ実行できる。

```sh
make -C EpgTimerSrv/EpgTimerSrv
```

## 変更概要

- `CEpgTimerSrvSetting::SETTING` に `SyncResAutoAdd*` 4項目を追加した。
- `EpgTimerSrv.ini` の `[SET]` から `SyncResAutoAddChange` / `SyncResAutoAddDelete` / `SyncResAutoAddChgNewRes` / `SyncResAutoAddChgKeepRecTag` を読み込むようにした。
- 自動予約登録の変更・削除時に既存予約を同期するサーバ側共通処理を `CEpgTimerSrvMain` に追加した。
- CtrlCmd、CMD_VER対応CtrlCmd、旧互換CtrlCmd、Lua API の変更・削除経路から同期処理を呼ぶようにした。
- EMWUI の画面修正はユーザー指示により今回の作業から外した。

## 検証

次を実行し、ビルド成功を確認した。

```sh
make -C EpgTimerSrv/EpgTimerSrv
```

`systemctl` と EDCB の再起動は実行していない。
