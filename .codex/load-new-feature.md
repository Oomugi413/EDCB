# 起動中の EDCB に今回の変更を反映する手順

## 前提

この文書は、EDCB が初回起動時に次のコマンドで起動された環境を前提にする。

```sh
sudo systemctl start edcb
```

`edcb.service` の `ExecStart` は、EDCB の Unix ビルド手順どおり `/usr/local/bin/EpgTimerSrv` を指している前提。

今回の変更は `EpgTimerSrv` 本体のコード変更なので、`EpgTimerSrv.ini` の `ReloadSetting` だけでは反映できない。新しいバイナリをインストールしたうえで、`edcb` サービスを再起動する必要がある。

## 1. 現在の状態確認

```sh
cd ~/git/EDCB
sudo systemctl status edcb --no-pager
```

現在稼働中の実行ファイルを確認する。

```sh
pidof EpgTimerSrv
readlink -f /proc/$(pidof EpgTimerSrv)/exe
ls -l /usr/local/bin/EpgTimerSrv
```

## 2. ビルド

```sh
cd ~/git/EDCB
make -C EpgTimerSrv/EpgTimerSrv
```

## 3. 現在のインストール済みバイナリを退避

```sh
sudo cp -a /usr/local/bin/EpgTimerSrv /usr/local/bin/EpgTimerSrv.before-linux-reserve-feature.$(date +%Y%m%d%H%M%S)
```

## 4. 新しいバイナリをインストール

稼働中プロセスが参照している実行ファイルを直接書き換えず、一時ファイルを置いてから `mv` で差し替える。

```sh
cd ~/git/EDCB
sudo install -m 755 EpgTimerSrv/EpgTimerSrv/EpgTimerSrv /usr/local/bin/EpgTimerSrv.new
sudo mv /usr/local/bin/EpgTimerSrv.new /usr/local/bin/EpgTimerSrv
sudo chown root:root /usr/local/bin/EpgTimerSrv
```

インストール後のファイルを確認する。

```sh
ls -l /usr/local/bin/EpgTimerSrv
```

## 5. 設定を有効化する場合

今回追加した設定は `EpgTimerSrv.ini` の `[SET]` から読み込まれる。設定ファイルは通常 `/var/local/edcb/EpgTimerSrv.ini`。

まず設定ファイルを退避する。

```sh
sudo cp -a /var/local/edcb/EpgTimerSrv.ini /var/local/edcb/EpgTimerSrv.ini.before-linux-reserve-feature.$(date +%Y%m%d%H%M%S)
```

例として、変更時に旧条件の予約を作り直し、削除時にも対象予約を削除する設定にする場合は次を実行する。

```sh
sudo perl -0pi -e 's/\nSyncResAutoAddChange=.*//g; s/\nSyncResAutoAddDelete=.*//g; s/\nSyncResAutoAddChgNewRes=.*//g; s/\nSyncResAutoAddChgKeepRecTag=.*//g; s/\[SET\]/[SET]\nSyncResAutoAddChange=1\nSyncResAutoAddDelete=1\nSyncResAutoAddChgNewRes=1\nSyncResAutoAddChgKeepRecTag=0/' /var/local/edcb/EpgTimerSrv.ini
```

設定値を確認する。

```sh
grep -E '^(SyncResAutoAddChange|SyncResAutoAddDelete|SyncResAutoAddChgNewRes|SyncResAutoAddChgKeepRecTag)=' /var/local/edcb/EpgTimerSrv.ini
```

## 6. EDCB を再起動して新バイナリを反映

録画中でないことを確認してから実行する。録画中に実行すると録画に影響する可能性がある。

```sh
sudo systemctl restart edcb
```

再起動後の状態を確認する。

```sh
sudo systemctl status edcb --no-pager
pidof EpgTimerSrv
readlink -f /proc/$(pidof EpgTimerSrv)/exe
```

## 7. ログ確認

```sh
journalctl -u edcb -n 100 --no-pager
```

EDCB 側のログファイルも確認する場合。

```sh
ls -l /var/local/edcb
tail -n 100 /var/local/edcb/EpgTimerSrvDebugLog.txt
```

`EpgTimerSrvDebugLog.txt` が存在しない、またはデバッグログを保存していない設定の場合、この `tail` は失敗する。その場合は `journalctl` の確認だけでよい。

## 8. 問題が出た場合のバイナリ復旧

退避したバイナリ名を確認する。

```sh
ls -lt /usr/local/bin/EpgTimerSrv.before-linux-reserve-feature.*
```

最新の退避バイナリへ戻す例。

```sh
sudo cp -a $(ls -t /usr/local/bin/EpgTimerSrv.before-linux-reserve-feature.* | head -n 1) /usr/local/bin/EpgTimerSrv
sudo systemctl restart edcb
sudo systemctl status edcb --no-pager
```

## 9. 問題が出た場合の設定復旧

退避した設定ファイル名を確認する。

```sh
ls -lt /var/local/edcb/EpgTimerSrv.ini.before-linux-reserve-feature.*
```

最新の退避設定へ戻す例。

```sh
sudo cp -a $(ls -t /var/local/edcb/EpgTimerSrv.ini.before-linux-reserve-feature.* | head -n 1) /var/local/edcb/EpgTimerSrv.ini
sudo systemctl restart edcb
sudo systemctl status edcb --no-pager
```

## 補足

- 新しいコード自体の反映には `sudo systemctl restart edcb` が必要。
- 設定値だけを後から変える場合は、WebUI や Lua の `ReloadSetting` で反映できる可能性があるが、今回の新機能を初回反映する時点ではプロセス再起動が必要。
- `sudo systemctl start edcb` は停止中の初回起動用。すでに起動中のプロセスへ新バイナリを反映する場合は `sudo systemctl restart edcb` を使う。
