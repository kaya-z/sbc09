# MK09 — MC6809 のマイクロカーネル

MK09 は、MC6809 (`sim09` と Tang Nano 9K の FPGA) で動く小さなマイクロカーネルです。**64KB のメモリだけ**で動き (MMU は使わない)、
カーネルもプログラムも**SD カードから起動時に読み込まれます**。カーネルがするのはタスク、同期メッセージ、タイマ、メモリ領域の検査だけで、
コンソール、ディスク、FAT16 のファイルシステム、プログラムの読み込みは、メッセージで話すサーバ (普通のタスク) がします。
その上で、シェル、コマンド、**日本語ワードプロセッサ `jwp`** (SKK 方式の仮名漢字変換つき。辞書はメモリではなく SD カードの上) が動きます。

* カーネル: 6809 のアセンブラ (標準、2KB) と、移植用の C (同じテストを通る)
* サーバ (C): TTY (日本語の行編集)、DISK、FS (FAT16, PC で読み書きできる)、PM (プログラムの読み込み)
* ブート ROM は FUZIX と同じ 256 バイト。**FUZIX 用の FPGA ビットストリームがそのまま使えます** (SD カードを替えるだけ)

| 文書 | 内容 |
|---|---|
| [docs/MANUAL.md](docs/MANUAL.md) | ユーザーマニュアル: ビルド、起動、SD カード、コマンド、`jwp` の使い方、プログラムの作り方 |
| [docs/SPEC.md](docs/SPEC.md) | 仕様書: システムコール、メッセージ、サーバの要求、ファイルの形式、ブートイメージ、ハードウェア |
| [docs/DESIGN.md](docs/DESIGN.md) | 設計書: 構成、メモリの配分、カーネルとサーバの設計、移植性、性能 |

## すぐ動かす

```sh
../tools/install_cmoc.sh $HOME/cmoc        # 6809 の C コンパイラ (CMOC) と lwtools。一度だけ
make -C .. sim09                           # シミュレータ
make CMOC_BIN=$HOME/cmoc/bin               # build/ にブート ROM とディスクイメージ
../sim09 -rom build/bootrom.bin -0 build/mk09.img -kanji utf8
```

`/> ` が出たら `help`、日本語ワープロは `jwp メモ.txt` (`^J` でかな入力、`^Q` で終了)。止めるのは `shutdown`。
実機は SD カードに `dd if=build/mk09.img of=/dev/sdX bs=512 conv=fsync` (デバイス名を必ず確かめてください)。

## テスト

```sh
make test      # 自動テスト一式 (EUC-JP / Shift_JIS 版も作ります。数分かかります)
```

## ライセンス

MK09 の著作者は **YUKARI Semiconductor Devices** です (Copyright (c) 2026 YUKARI Semiconductor Devices)。
このディレクトリのソース (`mk09/` のうち、このプロジェクトで書いたもの) は MIT ライセンスです (ファイルの先頭の `SPDX-License-Identifier`)。

* ビルドしたプログラム (`*.mx`、ブートイメージ) には、CMOC の実行時ライブラリ (GPL v3、Pierre Sarrazin) が入ります。配布するときは GPL v3 の条件に従ってください。
* `apps/jwp/` は `os9/jwp/` (MIT) からの派生です。辞書は Debian の `skkdic` の SKK-JISYO.L (GPL v2 以降) から作ります。
* `apps/dhry.c` の手続き部分は Dhrystone 2.1 (Reinhold P. Weicker) で、原作者の著作物です。ライセンス文がないため、扱いは `../LICENSES.md` の 4.2 に従います。時間計測と出力の部分は MIT です。
* `boot/bootrom.s` は `fuzix/bootrom.s` と同じ形式の ROM で、同じバイト列を作ります。
* 全体の一覧はリポジトリの [../LICENSES.md](../LICENSES.md) と [../CREDITS.md](../CREDITS.md) (このディレクトリのファイルは、そこにはまだ載っていません)。
