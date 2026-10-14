# FUZIX (Alan Cox ほか) の移植

`sim09` の Level 1 マシン (**64KB のフラットメモリ、MMU なし**) で、UNIX 風 OS の [FUZIX](https://github.com/EtchedPixels/FUZIX)
が動きます。メインメモリは 64KB に収まっており、MMU は使っていません (Level 2 の MMU も不要です)。

```sh
make -C . all
gzip -dc fuzix/images/sim09-fuzix.dsk.gz > /tmp/fuzix.dsk
./sim09 -rom fuzix/images/bootrom.bin -0 /tmp/fuzix.dsk          # 速く動かすなら -max
```

`login:` に `root` と打つとシェルが出ます (パスワードなし)。**止めるときは `shutdown`** を使ってください
(`Halted.` を出してシミュレータが終了コード 0 で終わります)。`halt` は init の後始末 (`rc.halt`) を通さないので、
次の起動でファイルシステムが「not cleanly unmounted」と検査されます。ディスクは書き込まれるので、毎回きれいな状態から
始めたいときは `.gz` から展開し直してください。

## 設計

| 項目 | 内容 |
|---|---|
| 移植の方式 | FUZIX の `platform-sim09` を追加 (`fuzix/platform-sim09/`)。FUZIX の coco2 / multicomp09 のポートを手本にした。カーネルとプログラムは 6809 (`CPU=6809`) |
| メモリ | 64KB 全部が RAM。ROM は `$FF00-$FFFF` の 256 バイトだけ。I/O は `$E000-$E1FF` |
| プロセス | **スワップのみ** (`CONFIG_SWAP_ONLY`)。ユーザ空間は 1 つ (`$6C00-$DFFF`、29KiB) で、プロセスを切り替えるたびに全体をディスクのスワップ領域に書き出し/読み込みする。MMU がないのでメモリ保護はない |
| ディスク | シミュレータのディスク DMA ポート (`$E040-`) を `tinydisk` の 512 バイトブロックで使う。MBR のパーティション: 1 = ファイルシステム (type `0x7E`)、2 = スワップ (type `0x7F`、`CONFIG_DYNAMIC_SWAP` が検出) |
| コンソール | ACIA0 (`$E000/$E001`) をポーリング。ACIA の割り込みはない |
| 時計 | タイマ (`$E030`、モード 2) を 50Hz で割り込みにする。`/dev/rtc` は `$E031-$E036` の RTC を読む (起動時のホストの時刻 + エミュレートした経過時間) ので、起動時に日付を聞かれない |
| 停止 | `$E00F` に `$A5` を書くとシミュレータが終了する (`shutdown` / `halt` の最後) |
| 使うプログラム | 完全な FUZIX の全コマンドではなく、`build-mini-filesystem` の最小セット (V7 のコマンド、`util` の一部、シェル) |

### メモリマップ

```
0000-00FF  割り込みスタック
0100-0111  ベクタのジャンプ表 (SWI3 0100, SWI2 0103, SWI/システムコール 0106, NMI 0109, IRQ 010C, FIRQ 010F)
0200-03FF  ブートのバッファ、ディスクの一時領域 (reserve)
0405-6B34  カーネルのコード
6C00-DFFF  ユーザプログラム (PROGBASE-PROGTOP, 29KiB。スワップの単位)
E000-E1FF  I/O ページ
E200-FB02  カーネルのデータと bss、バッファ (NBUFS 4)
FD00-FEFF  udata (プロセスごとの構造体。スワップのときに退避)
FF00-FFFF  ブート ROM とリセット/割り込みベクタ
```

(`.discard` セクションは `$A000` に置かれ、起動後に捨てられる。ユーザ空間がその上に重なるので、起動処理は 1 回きりです。)

### ブート

1. リセット: `fuzix/bootrom.s` (256 バイト) がドライブ 0 の LSN 2 から、カーネルイメージを `[宛先上位][宛先下位][個数][データ]` の
   セクタ列として読み込む (`個数` が 0 のセクタが入口)。`fuzix/platform-sim09/mkimage.py` が `lwlink` の出力から作る。
2. カーネルが `devhd_init` でドライブ 0/1 を確認し、`tinydisk` が MBR を読み、`hda1` をルートにする。
3. `init` が `/etc/rc`、`getty`、`login` を始める。

ディスクの配置 (`fuzix/mkdisk.py`): ブロック 0 = MBR、ブロック 1.. = カーネルイメージ (最大 1MB)、ブロック 2048.. = ファイルシステム (2MB)、
続けてスワップ (1MB)。ドライブ 1 (`-1 file`) は `hdb` として使える (空でも起動する)。

## 作り方

ビルド済みの `fuzix/images/bootrom.bin` と `sim09-fuzix.dsk.gz` を同梱しています。作り直すには、6809 用の gcc 4.6.4
(FUZIX が使う、lwtools と組み合わせたもの) が要ります。

```sh
tools/install_cmoc.sh $LW                       # lwtools (lwasm, lwlink, lwar) を $LW/bin に入れる (CMOC も入る)
LWSRC=/path/to/lwtools-source-tree \
  tools/build_gcc6809.sh $WORK/gcc $PREFIX $LW/bin   # gcc 4.6.4 を m6809-unknown-gcc として作る (ftp.gnu.org から取得)
PATH=$PREFIX/bin:$LW/bin:$PATH fuzix/build.sh $WORK/fuzix
```

`fuzix/build.sh` は FUZIX を GitHub から決まった版 (`776bfc9…`、2025-09-02) で取得し、`platform-sim09` を足して、
`ltools libs stand kernel` とコマンドをビルドし、ルートのファイルシステムとディスクを作ります。必要なもの:
perl, python3, gcc, make, yacc (bison -y), git。ネットワークが要ります (GitHub, ftp.gnu.org)。

`make test-fuzix` が起動から `shutdown` までの 17 項目の検査 (パイプ、`ps`、`df`、日付、再起動後のファイル、`hdb` など)を行います (`tests/fuzix_test.py`)。

## 制限

* **プロセスのメモリは 29KiB** (コード + データ + bss + スタック)。`ls` のように大きなコマンドは、ファイルが多いディレクトリで
  メモリ不足になる (`/bin` の一覧で `out of memory` が出ることがある)。そのときは名前を絞る。
* 切り替えのたびにスワップするので、複数のプロセスを同時に動かすと遅い (動作はする。パイプと `ps` はテストで確認、バックグラウンドジョブも実行して確認)。
* メモリ保護はない。ユーザプログラムが壊したいと思えばカーネルも壊せる (MMU のない 6809 の FUZIX 一般の制限)。
* 入っているコマンドは最小セットのみ (`sleep` や `vi` はない)。コンパイラ、エディタ、ゲームなどの FUZIX の他のアプリは入れていない。
  入れる場合は、`Applications` のビルドと `fuzix-mini.pkg` の差し替えが要る (スワップの 29KiB に収まるものに限る)。
* ネットワーク (OS-9 Net 相当) はない。ACIA は 1 つだけ (`/dev/tty1`)。
* 割り込みで ACIA を受けないので、入力は 50Hz のタイマのたびに取る。
* アイドル時は `sync` 命令で割り込みを待つ (`_plt_idle`)。実時間ペースでのホスト CPU 使用率は測っていない。

## ライセンス

* `fuzix/platform-sim09/` は FUZIX の coco2 / multicomp09 のポートから派生したので **GPL-2.0-only** (ソースと Makefile の先頭に表記。`fuzix.link` などの設定ファイルは表記なしで同じ扱い)。
* `fuzix/build.sh` `bootrom.s` `mkdisk.py` `tests/fuzix_test.py` `tools/build_gcc6809.sh` はこのプロジェクトの **MIT**。
  ただし `bootrom.bin` はそのアセンブル結果なので MIT。
* ディスクイメージ `sim09-fuzix.dsk.gz` は FUZIX のカーネル (GPL-2.0)、C ライブラリ (LGPL-2.1)、`util` のコマンド (GPL-2.0)、
  V7 由来のコマンドとシェル (Caldera の BSD 形式) を含むので、配布するときはそれぞれの条件に従う。
  LGPL のライブラリは各コマンドに静的に結合されている。詳しくは [LICENSES.md](../LICENSES.md) の 2.1。
