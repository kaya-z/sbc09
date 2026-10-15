# ライセンスの一覧

このリポジトリは**単一のライセンスではありません**。ファイルの出所によって次のように分かれます。
これは技術的な整理であり、法的な助言ではありません。再配布や商用利用の前に、各上流のライセンスを確認してください
(上流の記載を確認できたものは出典を付け、確認できなかったものはその旨を書いています。調査日: 2026-10-08)。

| 記号 | ライセンス | 全文 |
|---|---|---|
| **MIT** | MIT License (Copyright (c) 2026 sim09 contributors) | [LICENSES/MIT.txt](LICENSES/MIT.txt) |
| **GPL-2.0** | GNU General Public License version 2 | [LICENSES/GPL-2.0.txt](LICENSES/GPL-2.0.txt) |
| **GPL-3.0** | GNU General Public License version 3 (CMOC の実行時ライブラリを含むバイナリ) | [LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt) |
| **個別** | 第三者の著作物で、ファイルごとに条件が異なる、または条件が不明確なもの | 4 章 |

MIT の著作権者の表記 `sim09 contributors` は仮のものです。実際の著作権者の名前に置き換えてください
(`LICENSES/MIT.txt` と下の表)。

## 1. このプロジェクトで書いたもの: MIT

ファイルの先頭に `SPDX-License-Identifier: MIT` があります。

| 場所 | 内容 | 補足 |
|---|---|---|
| `src/*.c` `src/*.h` (`os9names.h` を除く) | シミュレータ (CPU コア、マシン、コンソール、ネットワーク、VRBF、モニタ、逆アセンブラ) | sbc09 の `v09s.c` は**命令仕様の参照**、MAME の `m6809.cpp` は**クロック/サイクル/状態の考え方**を参考にした独自の実装で、両者のコードは複製していません。参考にしたことは各ファイルの先頭と `CREDITS.md` に書いてあります。`diff_v09s.c` が `v09s.c` と挙動を突き合わせるのも、複製ではなく検証です |
| `tests/*.py` `tests/*.c` `tests/crosscheck/crosscheck.sh` | テスト | `tests/diff_v09s.c` は、実行時に外部の `v09s.c` を `#include` します (そのファイルはリポジトリに入っていません)。そのテストのバイナリは GPL v2 の `v09s.c` を含むため、配布する場合は GPL v2 になります |
| `tools/mkskkdic.py` `mkrbf.py` `mkboot.py` `os9edition.py` `dhry_bench.py` `install_cmoc.sh` `fix_org.py` `gen_os9names.py` `trace_cmp.sh` | ツール | `mkskkdic.py` が**生成する辞書**は入力の SKK 辞書 (GPL v2 以降) の派生物です |
| `os9/jwp/jwp.c` `skk.c` `genmsgs.py` `Makefile` と、生成される `msgs.h` | ワードプロセッサ `jwp` と SKK 入力 (**著作権者は YUKARI Semiconductor Devices**、各ファイルの先頭に Copyright 行) | SKK は入力方式に従った独自実装で、SKK のコードは使っていません。**ビルド済みのモジュール `jwp` `jwp-l2` は GPL-3.0 です** (3 章) |
| `os9/dhry/Makefile`、`dhry.c` の時間計測と OS-9 呼び出しの部分 | Dhrystone の実行部分 | Dhrystone 本体は原作者の著作物で、ライセンス文がない (4.2) |
| `os9/skk/Makefile` | 辞書ディスクの作成 | 辞書は同梱していません (手元の `skkdic` から生成) |
| `os9/misc/shutdown.asm` `os9/net/nc.asm` `os9/kanji/kmode.asm` と、`os9/kanji` `os9/net` `os9/misc` の `defsfile` | OS-9 のコマンド | NitrOS-9 の定義ファイル (`os9/nitros9-code/defs`) を `use` してアセンブルします (定義ファイルの内容は埋め込んでいません) |
| `Makefile` `os9/Makefile` `os9/level1/cmds/Makefile` `os9/level2/cmds/Makefile` | ビルド | |
| `fuzix/build.sh` `fuzix/bootrom.s` `fuzix/mkdisk.py` `tools/build_gcc6809.sh` `tests/fuzix_test.py` | FUZIX の移植のビルド、ブート ROM (`bootrom.bin` はそのアセンブル結果)、ディスク作成、テスト | `platform-sim09/` は 2.1 章 |
| `fpga/tn9k/rtl/*` `sim/*` `scripts/*` `constraints/*` `tests/fpga_test.py` `tests/hdmi_*.py` | FPGA 版の RTL、Verilator のテストベンチ、ビルド | `vendor/` は 2.2 章 |
| `README.md` `CREDITS.md` `LICENSES.md` `docs/*.md` | 文書 | |

## 2. sbc09 から複製・派生したもの: GPL-2.0

上流の記載を確認しました: Shinji Kono 版 [shinji-kono/sbc09](https://github.com/shinji-kono/sbc09) は `LICENSE` が GPL v2 (GitHub の判定も GPL-2.0)、その元の sbc09 (例: [cmarrin/6809 の sbc09](https://github.com/cmarrin/6809/tree/main/sbc09)) は `COPYING` が GPL v2 で、README にも「GPL」とあります。`a09.c` の先頭にも「GNU General Public License version 2」とあります。

sbc09 (Lennart Benschop、Jens Diemer、Johann E. Klasek ほか、および Shinji Kono の OS-9 対応版) のファイルです。複製、または
複製に手を入れたものなので、**元のまま GPL v2 です**。

| 場所 | 内容 |
|---|---|
| `tools/a09.c` `os9crc.c` `crc.c` `makerom.c` `os9mod.c` `os9.h` と、そのビルド結果 `a09` `make9rom` `os9mod` | アセンブラと ROM 作成ツール |
| `os9/level1/*` `os9/level2/*` (下の例外を除く) | sbc09 用のモジュール (`pdisk` `pty` `vrbf` `boot` `sysgo` `d0` `d1` `v0` `clock` `init` `vector` など)。**私が修正したもの** (`level1/clock.asm`、`level*/pdisk.asm` など) も、元が GPL v2 なので GPL v2 です |
| `os9/OS9.dsk` `WORK.dsk` `v09.rom` | ディスクイメージと ROM (sbc09 のもの) |
| `os9/v*.rom` `os9v*_euc.rom` `os9v*_sjis.rom` | 上のモジュールと NitrOS-9 から組み立てた ROM |
| `os9/net/npty.asm` `nport.asm` `login.asm` | OS-9 Net のドライバ、デスクリプタ、ログイン。独自に書いたものですが、構造が sbc09 の `pty` ドライバ・デスクリプタと NitrOS-9 の `login` に近いので、**保守的に GPL v2 (および NitrOS-9 の条件) の側**に置いています |
| `tests/crosscheck/refpatch.py` | 参照シミュレータを改造する検証用スクリプト。参照ソースの断片 (置換の対象) を含みます |
| `AUTHORS.sbc09` | sbc09 の作者表 |

例外: `os9/level1/cmds/Makefile` `os9/level2/cmds/Makefile` と、ディスク起動用に新しく書いた `os9/level1/boot.asm` は MIT (1 章。ただし `boot.asm` は NitrOS-9 の `boot_common.asm` を `use` して 1 つのモジュールにするので、できた `Boot` モジュールには NitrOS-9 の条件も掛かります)、`os9/level2/ioman.asm` は NitrOS-9 の複製に修正を加えたもので 4 章です。

### 2.1 FUZIX の移植とディスクイメージ: GPL-2.0 (ライブラリは LGPL)

調べたこと: FUZIX ([EtchedPixels/FUZIX](https://github.com/EtchedPixels/FUZIX)) の `LICENCE` は「ライブラリは LGPL、一部のファイルはより緩い。アプリは各ディレクトリのライセンスを見ること」と書き、
GPL v2 の全文を付けています。`Library/COPYING` (LGPL)、`Applications/util/COPYING` (「このファイルは GPL v2 のユーティリティに適用、ほかは各アプリを見る」)、
`Applications/V7/COPYRIGHT` (UNIX V7 / 32V の Caldera International による BSD 形式の条件。著作権表示と免責の保持が要る) があります。

| 場所 | 扱い |
|---|---|
| `fuzix/platform-sim09/*` | FUZIX の coco2 / multicomp09 のポートの派生なので **GPL-2.0-only** (ソースと Makefile の先頭に表記。`fuzix.link` などの設定ファイルは表記なしで同じ扱い) |
| `fuzix/images/sim09-fuzix.dsk.gz` | FUZIX のカーネル (GPL v2)、C ライブラリを静的に結合した各コマンド (LGPL)、`util` のコマンド (GPL v2)、V7 由来のコマンドとシェル (Caldera の BSD 形式) の集まり。**配布するときは、それぞれの条件 (ソースの提供、著作権表示) に従う**。FUZIX のソースは `fuzix/build.sh` が固定の版で取得できる |
| `fuzix/images/bootrom.bin` | `fuzix/bootrom.s` (MIT) をアセンブルしたもの |

`util` と V7 のコマンドのうち、ディスクに入れたそれぞれのファイルごとの表記は個別には確認していません (`Applications/util` の一部には GPL v2 以外のものがあると COPYING に書かれています)。
再配布の前に、入れるコマンドを `fuzix/build.sh` の取得物で確認してください。

### 2.2 FPGA 版にいれた第三者の RTL (`fpga/tn9k/vendor/`)

| 場所 | ライセンス | 確認したこと |
|---|---|---|
| `vendor/mc6809/mc6809i.v` (Greg Miller の MC6809 コア) | **BSD-3-Clause** | 上流 [cavnex/mc6809](https://github.com/cavnex/mc6809) の `documentation/LICENSE.md` が 2 つを示す: 標準の BSD (ソース再配布可、著作権表示を残す) と、ソースを再配布しない場合の修正版。**標準の BSD** を選んだ (`vendor/mc6809/LICENSE.md` に全文)。1 か所だけ変更した (ファイルの先頭に明記) |
| `vendor/psram/psram_controller.v` (Feng Zhou の PSRAM コントローラ) | **Apache-2.0** | 上流 [zf3/psram-tang-nano-9k](https://github.com/zf3/psram-tang-nano-9k) の `LICENSE.txt` (`vendor/psram/LICENSE.txt` に全文)。変更なし |

| `vendor/shinonome/*.bit` (東雲フォントの 16 ドットの元データ: ASCII / Latin-1 / 半角カナ / JIS X 0208 の字形) | **Public Domain** | 作者が `LICENSE` で「すべて Public Domain で提供する」と書き、日本では権利の放棄ができないので、`AUTHORS` の作者が権利を行使しないと宣言することで実質的な Public Domain とする、とある。原本のまま (`vendor/shinonome/LICENSE` `AUTHORS`)。ここから `scripts/mkfont.py` が文字ROM を、`scripts/mkkanji.py` が漢字のイメージ (ボードのフラッシュに書く `kanji.bin`) を作る |

FPGA のビットストリームは、この 3 つと自作の RTL (MIT)、載せた ROM を含みます。ROM が FUZIX のブート ROM (MIT) なら、配布の条件は上の 3 つの表示です。NitrOS-9 の ROM を載せたもの
(`build.sh os9`) は 4.1 章の条件も付きます。

## 3. CMOC の実行時ライブラリを含むバイナリ: GPL-3.0

`os9/jwp/jwp` `os9/jwp/jwp-l2` `os9/dhry/dhry` (と `os9/level*/cmds/` にある同じもの、`jwp_chk`) は、CMOC (Pierre Sarrazin、GPL v3) の
実行時ライブラリ (起動コード、16 ビット乗除算、`memcpy` `strcat` `strcpy` `strlen`) を含むので、**モジュールとしては GPL v3 です**。
ソース (`jwp.c` など) は MIT ですが、ビルド済みのモジュールを配布するときは GPL v3 の条件 (ソースの提供など) に従ってください。
ビルド済みのモジュールを同梱せず、CMOC で各自ビルドしてもらう形にすれば、この条件は配布物から外れます。

## 4. 第三者の著作物で、ファイルごとに条件が異なる、または不明確なもの

### 4.1 NitrOS-9 (`os9/nitros9-code/` と、それを改造したもの)

調べたこと:

* 上流 [nitros9project/nitros9](https://github.com/nitros9project/nitros9) の**リポジトリには `LICENSE` などのライセンス文がありません** (トップは README、CONTRIBUTING、`archive/project-history` など)。
* [SourceForge の NitrOS-9 のページ](https://sourceforge.net/projects/nitros9/) は、プロジェクトのライセンスとして **「GNU General Public License version 2.0 (GPLv2)」** を表示しています。ただしこれはプロジェクトの申告で、各ファイルの条件を調べたものではありません。
* NitrOS-9 は、Microware の OS-9 (Tandy 向けなど) を元にしたコミュニティの配布で、元の OS-9 は[プロプライエタリ](https://en.wikipedia.org/wiki/OS-9)です。Microware や Tandy が、どの条件でソースの再配布を許したのかを示す文書は、見つけられませんでした。
* このリポジトリの複製 (347 ファイル) の中を調べると、ファイルごとに次のような表記があります。

| ファイル | 表記 | ビルドで使うか |
|---|---|---|
| `level2/modules/kernel/krnp2.asm` | `Copyright (c) 1982 Microware Corporation` (6309 対応などの改造は Gale Force Enterprises) | **使う** (Level 1 と Level 2 の ROM の `os9p2`) |
| `level2/modules/krnp3_perr.asm` | `COPYRIGHT (C) 1987 by Peter Durham` 「OS-9 コミュニティの全員が、個人の楽しみのために使用、改変、共有してよい。**商用利用は作者の同意なしには禁止**」 | **使う** (Level 2 の ROM の `os9p3_perr`) |
| `level2/modules/krnp4_regdump.asm` | `Copyright February,1989 by Ron Lammardo` (条件の記載なし) | **使う** (Level 2 の ROM の `os9p4_regdump`) |
| `level1/cmds/error.asm` | Bob Devries 「Released under the GNU public licence」 (版の指定なし) | **使う** (`error` コマンド) |
| `level1/cmds/config.asm` | プログラムが表示する文字列に `COPYRIGHT 1985 BY MICROWARE SYSTEMS CORP. REPRODUCED UNDER LICENSE TO TANDY CORP. ALL RIGHTS RESERVED` | **使う** (`config` コマンド) |
| `level2/modules/pipeman_named.asm` | `Copyright 1981, 1985, 1986, 1988 by Microware Systems Corporation. All Rights Reserved` | 作るが ROM には入れない |
| `level2/cmds/grfdrv.asm` | `Copyright (c) 1982 Microware Corporation` | 使わない |
| `level1/modules/vrn.asm` | Bruce Isted 「著作権表示を残せば自由に配布してよい。…許可なく…パッケージに…」 | 使わない |
| `level1/modules/emudsk.asm` | Alan DeKok 1996 「All Rights Reserved. **個人の使用に限り許可**」 | 使わない |
| `level1/modules/boot_wd1002.asm` `level1/cmds/ded.asm` | Bruce Isted、Doug DeMartinis の「All Rights Reserved」 | 使わない |
| `level1/cmds/httpd.b09` | GPL (v3 以降の条件文) | 使わない |
| `defs/ide.d` `scsi.d` `rbsuper.d` `level1/modules/rbsuper.asm` | `(C) 2004 Boisy G. Pitre - Licensed to Cloud-9` | 使わない |
| `ssp.asm` `sspak.asm` `megaread.asm` `xmode.asm` | 「パブリックドメインとして公開」 | 使わない (`xmode` は参考のみ) |
| 上記以外の大部分 (`scf.asm` `rbf.asm` `defs/os9.d` など) | **著作権やライセンスの表記なし** | 使う |

分かったこと / 分からないこと:

* 上流はプロジェクトとして GPL v2 を申告していますが、実際のファイルには **Microware の 1982 年の著作権表示**や、**非商用に限る許可** (Durham)、「All Rights Reserved」などがあり、一つのライセンスでは言い表せません。このリポジトリの ROM は、そのうち **Microware 由来のカーネル (`krnp2`) と、Level 2 では Durham の非商用の許可つきのモジュール (`os9p3_perr`) を含みます**。
* したがって、**NitrOS-9 由来の部分 (複製、ROM、`scf.asm` `dir.asm` `ioman.asm` などを改造したもの) の再配布条件は、確定できません**。個人の使用や研究に使う分には問題が少ないと思われますが、再配布や商用の利用では、元の著作権者の条件 (特に Microware と Durham) を別に確認する必要があります。
* `os9/kanji/scf.asm` `dir.asm` と `os9/level2/ioman.asm` は NitrOS-9 の複製を改造したものです。NitrOS-9 の申告 (GPL v2) に従って GPL v2 とみなして扱うのが、最も安全です。
* 使っていない問題のあるファイル (上の表の「使わない」) は、**複製から除く**ことができます (ビルドへの影響はありません)。ROM の `os9p3_perr` も、省くことが可能です (エラー番号の説明文が出なくなるだけです)。
* `src/os9names.h` は、NitrOS-9 の `os9.d` から `tools/gen_os9names.py` が生成した、システムコールとエラーの名前の表です。名前と番号だけですが、NitrOS-9 由来として扱います。

### 4.2 Dhrystone (`os9/dhry/dhry.c`)

調べたこと:

* 原作者 Reinhold P. Weicker の配布物 ([netlib の `benchmark/dhry-c`](http://www.netlib.org/benchmark/dhry-c)、Version 2.1) のソースの先頭には、著者、版、日付、出典の表記だけがあり、**ライセンス文も、再配布の許可も、禁止も書かれていません**。付属の文書 (RATIONALE) には、測定の約束 (ground rules) と、ベンチマークを広く配布する意図が書かれています。
* Linux カーネルは Dhrystone を [`lib/dhry.h` `lib/dhry_1.c`](https://github.com/torvalds/linux/blob/master/lib/dhry_1.c) に持ち、先頭の SPDX を `GPL-2.0-only OR BSD-2-Clause` としていますが、Weicker のヘッダは原文のままで、作者のライセンスを示すものではありません。U-Boot は「Dhrystone is widely available in the public domain」と書いています ([例](https://third-party-mirror.googlesource.com/u-boot/+/5e4aeb9b450dbefad6e7b47e39169527d64affa0/lib/dhry/dhry_1.c))。これは U-Boot 側の記述で、原作者が宣言したものではありません。
* **このリポジトリの Dhrystone の手続き (`Proc_1`..`Proc_8`、`Func_1`..`Func_3`) は、netlib の原本と、空白、括弧、構造体の代入の書き方を除いて同じです** (本文の比較で 97〜100%、測定ループは 90% 一致)。記憶から書き起こしたものですが、**実質的に原本の複製**として扱います。以前「構造から書き起こしたもの」と書いたのは、この点で不正確でした。

結論: **原作者の著作物で、明示のライセンスがありません**。広く再配布されている (netlib、Linux、U-Boot など) 事実はありますが、条件は示されていないので、MIT にはできず、再配布が許されると断言もできません。時間の計測と OS-9 の呼び出しの部分 (`now()`、`main()` の計測部分、`checks()` の書式など) だけが、このプロジェクトの MIT です。

### 4.3 そのほか

| 場所 | 状況 |
|---|---|
| `OS-9 Hardware-Software Source Book 1988 Edition (Microware).pdf` (リポジトリの直下) | **このプロジェクトのものではありません**。Microware の資料で、著作権のある資料です。リポジトリには含めず、配布もしないでください |

## 5. 参考にしただけで、コードを使っていないもの

| 参考にしたもの | ライセンス | 参考にした内容 |
|---|---|---|
| MAME [`m6809.cpp`](https://github.com/mamedev/mame/blob/master/src/devices/cpu/m6809/m6809.cpp) (Nathan Woods、Tim Lindner ほか) | BSD-3-Clause (ファイルの先頭の `license:BSD-3-Clause` で確認) | クロック、サイクル、CPU 状態の考え方。コードは使っていません (クレジットのみ) |
| sbc09 `v09s.c` | GPL v2 | 命令仕様の参照 |
| Shinji Kono 版 sbc09 の `engine.c` `io.c` `v09.c` `vdisk.c` | GPL v2 | メモリマップ、I/O の約束事、VRBF のプロトコル。互換のために同じ約束事を**別に実装**しています |
| SKK (佐藤雅彦氏ほか) | GPL v2 以降 | 入力方式 (大文字で読みを始める、`Space` で変換、など) |
| SKK 辞書 `SKK-JISYO.L` | GPL v2 以降 (辞書の先頭の著作権表示で確認。Debian の `skkdic` パッケージ) | リポジトリに**含めません**。`make -C os9/skk` が手元の `skkdic` から生成します。生成した辞書は GPL v2 以降です |
| DriveWire (Boisy Pitre ほか) | — | OS-9 Net の発想のみ |

## 6. 配布するときの目安

* **ソースだけ**を配布する: 1 章の MIT のファイルは自由です。2 章 (GPL v2) のファイルは GPL v2 の条件が付きます。4 章 (NitrOS-9 と Dhrystone) は、再配布の条件を確定できていないので、公開する場合は 4 章の表を見て、含めるものを選んでください。
* **バイナリ (ROM、ディスク、`jwp` `dhry`)** も配布する: ROM とディスクは GPL v2 (と NitrOS-9 の条件)、`jwp` `jwp-l2` `dhry` は GPL v3 です。FUZIX のディスクイメージは 2.1 章のとおりです。
* **MIT のファイルだけ**を配布したい: 1 章のファイルだけを取り出せます (`src/` `tests/` `tools/` の自作分、`os9/jwp` の `.c` と `.py` など)。ただし
  実行には、2〜4 章のファイル (ROM、ディスク、OS-9 のモジュール、NitrOS-9 のソース) が要るので、別に用意してもらう形になります。

## 7. SD カードの仕様と商標 (FPGA 版の `sd_disk.v`)

FPGA 版 (`fpga/tn9k/rtl/sd_disk.v`) は microSD カードを **SPI モード**で読み書きします。コードは**このプロジェクトで書いたもの (MIT)** で、SD アソシエーションの
仕様書の文章やコードを取り込んでいません。ただし、SD カードの仕様の扱いについては、次のとおり**公式には確認できていない点**があります (2026-10-13 時点。法的な助言ではありません)。

* SD アソシエーションの[ライセンス契約書](https://www.sdcard.org/pdf/SDA_License_Agreement.pdf) (SD Card Association License Agreement、2025 年版) は、会員に仕様書の使用を無償で許諾するものですが、
  **「SD ホスト製品」** (SD メモリカードを実際に扱い、Part 1 物理層と Part 2 ファイルシステムに適合する製品。1.18 項) を**作る、設計する、使う、売る**には、別に **SD-3C LLC の
  「Host/Ancillary Product License」** が必要だと書いています (2.9 項)。**SD ロゴ**を使うにも、同じ SD-3C のライセンスが必要です (2.11 項)。
* この契約書の全文に **「SPI」という語は出てきません**。SPI モードだけを使うホストがこの「SD ホスト製品」にあたるのか、あたらないのかは、**公式の文書では確認できませんでした**
  (非公式には、SPI モードは MMC と共通なので不要という意見と、ホストを作るなら要るという意見の両方があります)。
* このプロジェクトは、個人の趣味の目的で、ソースコードとして公開するもので、**SD ロゴは使っていません**。文書の中の「SD カード」「microSD」「SDHC」「SDXC」は、カードの種類を示すための呼び名です
  (これらは SD-3C LLC などの商標です)。
* **製品として販売する、ボードや製品に SD ロゴを付ける、SD カードを使う機器として出荷する**場合は、先に SD アソシエーションまたは SD-3C LLC に、SPI モードだけのホストでも
  ライセンスが要るかを問い合わせてください。

参考: Renesas のアプリケーションノート ([SPI モードの SD メモリカードドライバ](https://www.renesas.com/en/document/apn/rx-family-spi-mode-sd-memory-card-driver-firmware-integration-technology))も、
SD 仕様に適合するホスト機器を開発するときは SDALA と SDAMA を結ぶ必要があると書いています (自社製品向けの注意書き)。
