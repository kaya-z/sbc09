# OS-9 Net

シリアルポート ACIA1〜ACIA3 をホストの TCP ソケットに接続します。OS-9 からは通常のシリアルデバイス
`/n1` `/n2` `/n3` (SCF) に見えるので、シェル、リダイレクト、`tsmon`、BASIC09 など既存のソフトがそのまま使えます。

```
 OS-9 プログラム ──SCF──> NPty ドライバ ──ACIA レジスタ──> sim09 (net.c) ──TCP──> ホスト / インターネット
```

## 構成

| ファイル | 内容 |
|---|---|
| `src/net.c` | ホスト側。ソケット、telnet プロトコル、Hayes モデムのコマンド解釈、文字コード変換 |
| `os9/net/npty.asm` | SCF ドライバ `NPty` (PTY ドライバ + 切断検出 + SS.Ready) |
| `os9/net/nport.asm` | デバイスデスクリプタ `N1` `N2` `N3` (Level 1: `$E004/6/8`, Level 2: `$FF84/6/8`) |
| `os9/net/nc.asm` | `nc` ネットワーク端末/netcat 風クライアント |
| `os9/net/login.asm` | `tsmon` から呼ばれる最小の `login` (パスワードファイルなしでシェルへ) |

`make` で ROM (`os9v1.rom` / `os9v2.rom` など) に `NPty` `N1`..`N3` が入り、`nc` `login` はコマンドとしてビルドされます。

## `-net` オプション

```
-net N=listen:PORT[,raw][,any]    telnet サーバ
-net N=modem[:PORT][,any]         Hayes モデム
-net N=tcp:HOST:PORT[,telnet]     起動後 2 秒 (エミュレート時間) で接続する固定回線
```

`N` は 1〜3。`listen` / `modem:PORT` は既定で `127.0.0.1` にだけバインドします (外部に公開するときだけ `,any`)。

### listen: telnet 端末

`telnet localhost PORT` で接続すると `IAC WILL ECHO / WILL SGA` を送り、クライアントの `IAC` 列は取り除かれ、
`CR LF` / `CR NUL` は `CR` になります。文字コードは `-kanji` に従って変換されます。

```
OS9:chx /v0/cmds
OS9:tsmon /n1&              # 接続を待ち、最初の文字で login → Shell を起動。切断(EOF)でシェルが終わり次の接続を待つ
```

接続中の回線が塞がっていると 2 人目には `OS-9 line busy` を返して切断します。

### modem: ダイヤルアウト

回線は最初コマンドモードです (`nc /n2` で端末からも、`echo AT >/n2` のようにも使えます)。

| コマンド | 動作 |
|---|---|
| `AT` | `OK` |
| `ATDT host:port` (`ATD`) | TCP 接続。成功で `CONNECT`、拒否で `BUSY`、失敗で `NO CARRIER` (ポート省略時 23 = telnet) |
| `ATA` / `ATS0=1` | `modem:PORT` で待っている着信に応答 / 自動応答 (着信時 `RING`) |
| `+++` (前後 1 秒の無送信) | オンラインからコマンドモードへ (`OK`) |
| `ATO` | オンラインに戻る |
| `ATH` | 切断 |
| `ATE0/1` `ATV0/1` `ATZ` | エコー / 結果コードの文字表示 / 初期化 |
| `AT+TELNET=1` | 以降の接続で telnet のネゴシエーション除去と文字コード変換を行う |

接続が切れると `NO CARRIER` が受信データに入り、コマンドモードに戻ります。

### tcp: 固定回線

`-net 3=tcp:127.0.0.1:7000` なら `/n3` が最初から接続済みの生の TCP になります (OS-9 カーネルが起動時に I/O 空間を
16 ビットロードで走査してデータレジスタを読んでしまうため、接続は起動後 2 秒に遅らせています)。

## ACIA レジスタ (OS-9 から見た仕様)

| | |
|---|---|
| `+0` 状態 | b0 受信データあり (RDRF)、b1 送信可 (TDRE、常に 1)、b2 キャリア無し (/DCD)、b7 **HUP** (切断イベント) |
| `+1` データ | 読み: 受信 1 バイト。**空のとき読むと HUP を確認応答**。書き: 送信 |
| `+0` 書き込み | b0 = 1: DTR を落とす (ホストが回線を切る)。NPty は TERM (最後の close) で書く |

`NPty` は HUP を 1 回だけ読み取りエラー `E$EOF` として返し、`SS.Ready` も HUP のとき「準備完了」を返します
(切断を待っているプログラムが起こされる)。

## `nc` コマンド

```
nc <ポートデバイス> [host:port]
OS9:nc /n2 example.org:80        モデムでダイヤルして接続、キーボード ⇄ ネットワーク
OS9:nc /n3                       ダイヤルせずに回線にだけ接続 (tcp / listen ポート)
```

キーボードの CR は CR LF で送り、ネットワークから来た LF は CR LF で表示します。`^C` で切断して戻ります。
実行中だけ端末のエコー・自動 LF・大文字化を切り、終了時に元に戻します。

## 注意

* 受信した telnet のオプションは拒否も応答もしません (単に無視)。本格的な telnet クライアントとしては `nc` のほか、
  NitrOS-9 の `telnet`/`inetd` (DriveWire 向け) は使えません。
* 実時間のネットワークと付き合うので、ネットワークを使うときは `-max` を付けないでください
  (OS-9 は `F$Sleep 1` を「時間を譲る」として使うので、`-max` では OS-9 の時間がホストの実時間より速く進みます)。
* `tests/net_test.py` が telnet ログイン、日本語往復、再ログイン、`nc` のダイヤルアウトと切断検出を、
  Level 1/2 と UTF-8/EUC-JP/Shift_JIS で確認します。
