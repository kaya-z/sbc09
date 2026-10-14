# クレジット

このシミュレータは次のソフトウェアを参考にしています。各ソースの冒頭にも明記しています。

## sbc09 (Lennart Benschop, Jens Diemer, Johann E. Klasek ほか / GPL v2)
<https://github.com/cmarrin/6809/tree/main/sbc09>

* `v09s.c` を **MC6809 の命令仕様のリファレンス**にしました (`src/m6809.c`、`tests/diff_v09s.c` が突き合わせに使用)。
* `tools/a09.c` `os9crc.c` (6809 アセンブラ)、`tools/crc.c` `makerom.c` `os9mod.c` `os9.h` は sbc09 / Shinji Kono 版からの複製です。

## Shinji Kono の sbc09 (OS-9 対応版 / GPL v2)
<https://github.com/shinji-kono/sbc09>

* `engine.c` `io.c` `v09.c` `vdisk.c` `trace.c`: メモリマップ、MMU、I/O レジスタ規約、ディスクコマンド、
  VRBF (ホストのディレクトリを RBF として見せる) プロトコル、モニタのコマンド体系。`src/machine.c` `src/vdisk.c`
  `src/dbg.c` はこれらと同じ規約を実装した別実装で、**参考実装の ROM / ディスクイメージがそのまま動きます**。
* `os9/level1/*.asm` `os9/level2/*.asm` (pdisk, pty, vrbf, boot, sysgo, d0/d1/v0, clock, init, vector …)、
  `os9/OS9.dsk` `os9/WORK.dsk` `os9/v09.rom` は同プロジェクトのものです。`os9/level1/clock.asm` は
  不具合修正のため変更しています (README 参照)。

## MAME `m6809.cpp` (Nathan Woods, Tim Lindner ほか / BSD-3-Clause)
<https://github.com/mamedev/mame/blob/master/src/devices/cpu/m6809/m6809.cpp>

クロック／サイクル／CPU 状態 (`icount`、実行予算、SYNC/CWAI 状態、割り込み線、NMI の武装、LIC) の**考え方**を参考にしました。
コードは使っていません (対応表は `docs/CPU.md`)。

## NitrOS-9 プロジェクト
<https://github.com/nitros9project/nitros9>

`os9/nitros9-code/` はビルドに必要な NitrOS-9 のソース (defs、level1/level2 のモジュールとコマンド) の複製で、
同プロジェクトの申告 (GPL v2) と、各ファイルの表記に従います (LICENSES.md の 4.1)。`os9/kanji/scf.asm` と `os9/kanji/dir.asm` はその `scf.asm` / `dir.asm` の改造版です。

## DriveWire (Boisy Pitre, Aaron Wolfe)

`/n1`.. の仮想シリアルポートと `AT` コマンドで TCP に接続する、という OS-9 Net の発想は、NitrOS-9 / DriveWire のネットワーク
ポートに倣っています。コードは使っていません。

## CMOC / LWTOOLS (Pierre Sarrazin / William Astle, GPL v3 / GPL v3)
<http://sarrazip.com/dev/cmoc.html> <http://lwtools.projects.l-w.ca/>

日本語ワードプロセッサ `os9/jwp/jwp.c` は 6809 用 C コンパイラ **CMOC** (`--os9`) と、そのアセンブラ/リンカ **LWTOOLS** で
ビルドします (`tools/install_cmoc.sh`)。同梱のビルド済みモジュール `os9/jwp/jwp` `os9/jwp/jwp-l2` には CMOC の実行時ライブラリ
(起動コード、16 ビット乗除算、`memcpy` `strcat` `strcpy` `strlen`) が含まれるため、**モジュールとしては GPL v3** です。
`jwp.c` 自体は MIT です (LICENSES.md)。OS-9 のシステムコールの呼び出し方は CMOC のマニュアル (インラインアセンブラと OS-9 の節) に
従っています。コードの複製はありません。

## SKK (Masahiko Sato, Hironobu Takahashi ほか SKK 開発チーム / GPL v2 以降)
<https://github.com/skk-dev/dict> <https://ddskk.readthedocs.io/>

`jwp` の仮名漢字変換 (`os9/jwp/skk.c`) は、佐藤雅彦氏らの SKK (Simple Kana to Kanji conversion program) の**入力方式**
(大文字で読みを始める `▽`、`Space` で `▼` の候補、送り仮名の大文字、整列した辞書ファイルの二分探索、ユーザ辞書への学習)
に従った独自実装で、SKK のコードは使っていません。辞書は SKK の辞書 `SKK-JISYO.L` (Debian の `skkdic` パッケージ、GPL v2 以降) から
`tools/mkskkdic.py` で作ります。辞書そのものはこのリポジトリには入っていません (`make -C os9/skk` で手元の `skkdic` から生成)。

## Dhrystone (Reinhold P. Weicker)

`os9/dhry/dhry.c` は Reinhold P. Weicker の Dhrystone 2.1 (C 版、CACM 27(10), 1984) の手続きを、CMOC 用にまとめ直したもので、手続きの本体は原本と実質的に同じです
(原本にライセンス文はなく、再配布の条件は示されていません。確認した netlib の配布物のとおりです)。
時間を測る部分と OS-9 の呼び出しは新しく書いたもので、MIT です。実行時ライブラリ (CMOC) を含むビルド済みモジュールは GPL v3 です。

## FUZIX (Alan Cox ほか / GPL v2、ライブラリは LGPL v2.1)
<https://github.com/EtchedPixels/FUZIX>

`fuzix/platform-sim09/` は FUZIX のカーネルに加える**プラットフォーム部**で、同じ 6809 の coco2 / multicomp09 のポート
(`tricks.s` `commonmem.s` `libc.c` `crt0.s` `devtty` の構成ほか) を手本にして書き直し、一部を複製しています。FUZIX 本体はビルド時に
GitHub から取得し (`fuzix/build.sh`、版は固定)、リポジトリには入れていません。ディスクイメージ `fuzix/images/sim09-fuzix.dsk.gz` には
FUZIX のカーネル、C ライブラリ、`util` のコマンド、V7 由来のコマンドとシェルが入っています。

## gcc 6809 (William Astle、Lennart Benschop 版を元にした 4.6.4 へのパッチ / GPL v3)

FUZIX の 6809 のカーネルとコマンドは、gcc 4.6.4 に lwtools 対応のパッチ (`gcc6809lw-4.6.4-11.patch`、William Astle ほか) を当てたコンパイラ
でビルドします。`tools/build_gcc6809.sh` が gcc、gmp、mpfr、mpc を GNU のサーバから取得して作ります。コンパイラは配布物に含めていません。

## cavnex/mc6809 (Greg Miller / BSD-3-Clause)
<https://github.com/cavnex/mc6809>

`fpga/tn9k/vendor/mc6809/mc6809i.v` は Greg Miller のサイクル精度の MC6809 / MC6809E コア (Verilog) です。FPGA 版の CPU として**そのまま**使い、
変更は 1 か所だけです (割り込み系フリップフロップに電源投入時の値を与える。ファイルの先頭に書いてあります)。ライセンスは同梱の `LICENSE.md` の
標準の BSD (「ソース再配布用」) を選んでいます。

## 東雲フォント (古川泰之、/efont/ / Public Domain)
<http://openlab.ring.gr.jp/efont/shinonome/> (ミラー: <https://github.com/code4fukui/shinonome-font>)

FPGA の HDMI 文字画面の文字ROM (`fpga/tn9k/rtl/font8x16.hex`) は、東雲フォントの 16 ドットの ASCII / Latin-1 / 半角カタカナと、
漢字のイメージ (`kanji.bin`) は JIS X 0208 の 16x16 (記号・かな・ギリシャ・キリル・第 1 水準漢字)
(`fpga/tn9k/vendor/shinonome/` に原本のまま、`scripts/mkfont.py` と `scripts/mkkanji.py` で変換) です。作者が著作権を行使しないと宣言した事実上の Public Domain です
(同梱の `LICENSE` と `AUTHORS`)。

## psram-tang-nano-9k (Feng Zhou / Apache-2.0)
<https://github.com/zf3/psram-tang-nano-9k>

`fpga/tn9k/vendor/psram/psram_controller.v` は Tang Nano 9K のパッケージ内蔵 PSRAM (HyperRAM) のコントローラで、そのまま使っています。
`tn9k_pll.v` の PLL の設定の元は、この作者が動作確認した値 (81MHz、位相 90°) です。実機では 81MHz で PSRAM が誤るので、67.5MHz に下げました (docs/FPGA.md)。ボードのピン割り当ては Sipeed の回路図 (TF カード: 36-39 番ピン) と
Sipeed の例 (`TangNano-9K-example`) によります。

