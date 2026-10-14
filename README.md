# MC6809 / OS-9 シミュレータ (`sim09`)

C 言語で書かれた MC6809 シミュレータです。Debian 12 (gcc 12) でそのままビルド・実行できます。

* **OS-9 (NitrOS-9) Level 1 / Level 2** が動作します（参考実装
  <https://github.com/shinji-kono/sbc09> の ROM / ディスク構成と I/O 互換）
* **日本語処理**: UTF-8 / EUC-JP / Shift_JIS のコンソール、複数バイト文字を理解する行編集、日本語ファイル名
  → [docs/KANJI.md](docs/KANJI.md)
* **日本語ワードプロセッサ `jwp`**: OS-9 上で動く全画面エディタ (禁則つき自動折り返し、文字単位の編集、検索、整形出力、
  Level 1/2) と、**SKK 方式の仮名漢字変換** (辞書はフロッピー相当のディスクに置き、メモリに載せず二分探索) → [docs/JWP.md](docs/JWP.md)
  (端末・文字コード・メモリなどの機種依存: [docs/PORTING.md](docs/PORTING.md))
* **Dhrystone 2.1** (`dhry`): OS-9 上で動くベンチマーク。Level 1 / 2 とも約 210 Dhrystones/s (1.8432 MHz)、サイクル数による正確な測定ツールつき → [docs/DHRYSTONE.md](docs/DHRYSTONE.md)
* **FUZIX** (UNIX 風 OS): **64KB のフラットメモリ (MMU なし)** で起動してシェルまで動く。プロセスはスワップで切り替える (29KiB / プロセス)。
  `./sim09 -rom fuzix/images/bootrom.bin -0 fuzix.dsk` → [docs/FUZIX.md](docs/FUZIX.md)
* **FPGA (Tang Nano 9K)**: 同じマシン (64KB、ACIA、タイマ、ディスク) を **Sipeed Tang Nano 9K** の上に作る Verilog 設計 (サイクル精度の 6809 コア、
  RAM は内蔵 PSRAM、ディスクは microSD、コンソールは USB シリアルと **HDMI の文字画面** (80x25、ANSI 16 色、**漢字** (JIS 第 1 水準、UTF-8 / EUC-JP / Shift_JIS)))。FUZIX と NitrOS-9 Level 1 が動く (Verilator で検証。実機の Tang Nano 9K で FUZIX と NitrOS-9 の起動を確認、HDMI の映像は未確認: 試したモニタでは Sipeed のサンプルも映らなかった) → [docs/FPGA.md](docs/FPGA.md)
* **OS-9 Net**: シリアルポートを TCP ソケットに接続（telnet でログイン、Hayes モデム風の `ATDT` ダイヤル、`nc` コマンド）
  → [docs/OS9NET.md](docs/OS9NET.md)
* CPU コアは sbc09 `v09s.c` を命令仕様、MAME `m6809.cpp` をクロック／サイクル／CPU 状態の考え方の参考にした独自実装で、
  サイクル精度の命令タイミング、NMI/FIRQ/IRQ/RESET/HALT、CWAI/SYNC の待ち状態を持ちます
  → [docs/CPU.md](docs/CPU.md)
* 参考にしたソースのクレジットは各ソースの冒頭と [CREDITS.md](CREDITS.md) にあります。

## ビルドと実行

```sh
make                 # sim09、OS-9 ツール(a09 など)、ROM イメージ、OS-9 コマンドをビルド
make test            # 自己テスト一式
```

OS-9 Level 1:

```sh
./sim09 -rom os9/os9v1.rom -0 os9/OS9.dsk -1 os9/WORK.dsk -v os9/level1
```

OS-9 Level 1 を**ディスクの起動ファイル (`OS9Boot`) から**起動する (ROM はカーネルと `Boot` だけの 4KB。FPGA 版でも使えます → [docs/FPGA.md](docs/FPGA.md)):

```sh
./sim09 -rom os9/os9v1boot.rom -0 os9/OS9.dsk -1 os9/WORK.dsk -v os9/level1
```

OS-9 Level 2 (CoCo 3 風 MMU、2MB):

```sh
./sim09 -2 -rom os9/os9v2.rom -0 os9/OS9.dsk -1 os9/WORK.dsk -v os9/level2
```

`OS9:` プロンプトが出たら:

```
OS9:chx /v0/cmds          ホストの os9/level1/cmds にあるコマンドを実行パスに設定
OS9:dir /d0               OS9.dsk (RBF ディスクイメージ)
OS9:dir /v0               -v で指定したホストのディレクトリ (VRBF, 読み書き可)
OS9:mdir                  モジュールディレクトリ (Level 2 は mdir -e)
OS9:date                  時計 (起動時のホストの時刻 + エミュレートした経過時間)
```

### 終了 (shutdown)

```
OS9:chx /v0/cmds
OS9:shutdown          電源断レジスタ (I/O ベース+$0F に $A5) に書いてシミュレータを正常終了 (Level 1/2 共通)
```

そのほか、`Ctrl-]` でモニタに入り `q`、ネットワーク端末や `-script` からは `shutdown` / `quit` で終了できます。
OS-9 側にはディスクのキャッシュはなく (書き込みは即ホストへ)、`shutdown` 前の同期は不要です。

`Ctrl-]` (`-e` で変更可) でモニタ `v09>` に入ります (`h` でヘルプ: ステップ実行、ブレークポイント、
ダンプ、逆アセンブル、MMU 表示、S レコード、ディスク差し替え…)。`q` で終了します。

> `-v` で指したディレクトリは OS-9 から読み書きできます。ビルドディレクトリを直接指すときは注意してください。

### 日本語

```sh
# 端末が UTF-8 のとき: ゲストも UTF-8 (既定の ROM)
./sim09 -rom os9/os9v1.rom -0 os9/OS9.dsk -v os9/level1 -kanji utf8 -kname

# ゲストを EUC-JP / Shift_JIS にする (ホスト端末は UTF-8 のまま)
./sim09 -rom os9/os9v1_euc.rom  ... -kanji euc  -kname
./sim09 -rom os9/os9v1_sjis.rom ... -kanji sjis -kname
```

### 日本語ワードプロセッサ (jwp)

```
OS9:chx /v0/cmds
OS9:jwp memo.txt            全画面で編集。^S 保存  ^Q 終了  ^R 検索  ^X 整形出力 (キーの一覧は画面の最下行と docs/JWP.md)
```

端末の日本語入力でそのまま書けます。OS-9 の中だけで入力するなら SKK 方式の仮名漢字変換 (`^J` でかなモード、`Kanji␣` → 漢字)
が使え、辞書ディスクは `make -C os9/skk` で作ります (`SKK-JISYO.L` が要ります: `apt install skkdic`)。画面幅で禁則処理つきに折り返し、全角文字は 2 桁・カーソル/削除は文字単位です。
OS-9 の CR、ホストの LF / CRLF の改行を自動判定して同じ形式で保存します。

### OS-9 Net

```sh
./sim09 -rom os9/os9v1.rom -0 os9/OS9.dsk -v os9/level1 -net 1=listen:6809 -net 2=modem
```

```
OS9:chx /v0/cmds
OS9:tsmon /n1&              ポート 1 に telnet サーバ (ホストから: telnet localhost 6809)
OS9:nc /n2 example.org:80   ポート 2 のモデムでダイヤルして接続
```

## コマンドラインオプション

| オプション | 意味 |
|---|---|
| `-2` | Level 2 マシン (64KB 空間を 8KB x 8 の MMU マップで 2MB に拡張)。実行ファイル名が `sim09c` でも有効 |
| `-rom file` | ROM イメージ。省略時は `v09.rom` (BUGGY モニタ) |
| `-0 img` / `-1 img` | RBF の生ディスクイメージ (`/d0`, `/d1`) |
| `-v dir` (`-v1`..`-v3`) | ホストのディレクトリを `/v0` として見せる (VRBF) |
| `-m bytes` | Level 2 の物理メモリ (既定 2M, `512k` 以上) |
| `-l addr` | Level 1 の ROM 開始アドレス (既定: 64KB - ROM サイズ) |
| `-t file` `-tl lo` `-th hi` | 命令トレース (レジスタ + 逆アセンブル) |
| `-nt` | 起動直後にモニタに入る (タイマなし) |
| `-e char` | モニタに入るエスケープ文字 (既定 `0x1d` = Ctrl-]) |
| `-hz n` / `-clock hz` | タイマ割り込み周波数 (50) / CPU クロック (1843200) |
| `-max` | 実時間に合わせず全速で実行 (テスト向き) |
| `-kanji raw\|utf8\|euc\|sjis` | ゲストの文字コード。`raw` = 端末と同じ (変換なし) |
| `-hostcs name` | ホスト端末/ファイルの文字コード (既定: ロケールから) |
| `-kname` | `/v0` の日本語 (8 ビット) ファイル名を許可 |
| `-net N=...` | ネットワークポート (N = 1..3)。`listen:PORT[,raw][,any]` `modem[:PORT]` `tcp:HOST:PORT` |
| `-script file` | コンソールを expect/send スクリプトで操作 (テスト用、下記) |
| `-os9trace` `-watch lo[-hi]` `-stats` `-vdebug` | デバッグ: OS-9 システムコールのログ / 書き込み監視 / 速度表示 / VRBF トレース |

### スクリプト (`-script`)

```
timeout 20000          # expect の既定タイムアウト (エミュレートされた ms)
expect "OS9:"          # 出力にこの文字列が現れるまで待つ
send "dir /d0\r"       # 文字列を 1 文字ずつ入力 (\r \n \e \xHH \\)
sleep 500              # エミュレート時間で待つ
quit [code]
```

`sim09 -max -script t.txt` で OS-9 を決定的に自動操作できます (`tests/os9_test.py` が使っています)。

## ハードウェア構成 (参考実装 v09 / v09c と同一)

I/O ベース: Level 1 は `$E000`、Level 2 は `$FF80` (以下は I/O ベースからのオフセット)。

| オフセット | デバイス |
|---|---|
| `00/01` | ACIA0 制御・状態 / データ (コンソール)。状態: b0 受信有り, b1 送信可 |
| `04/05`, `06/07`, `08/09` | ACIA1..3 (OS-9 Net のネットワークポート `/n1`..`/n3`) |
| `11` | MMU タスクレジスタ (Level 2) |
| `20-27` / `28-2F` | MMU システム / ユーザマップ (Level 2, `$FFA0`/`$FFA8`) |
| `30` | タイマ制御: 書き込み `$8F` で開始/確認応答、`$80` 停止、`$04` で `31-36` に年月日時分秒 (RTC: 起動時のホストの時刻 + エミュレートした経過時間) |
| `40` | ディスク/VRBF コマンド (`$81` 読み、`$55` 書き、`$D1`-`$DD` VRBF) と `41-47` パラメータ |

Level 1 は 64KB のフラットメモリで ROM は書き込み保護、Level 2 は物理 2MB で `$FE00-$FFFF` が常に物理 `$7FE00` に固定されます。

## テスト

| コマンド | 内容 |
|---|---|
| `make test-cpu` | CPU コアの自己テスト (割り込み、CWAI/SYNC/HALT/RESET、NMI の武装、DAA、サイクル数 …) |
| `make test-diff` | 全オペコード (ページ 0/1/2) について v09s.c と乱数で突き合わせ (レジスタ・メモリ・サイクル) |
| `make test-os9` | Level 1/2 の起動、RBF / VRBF、時計、日本語 (UTF-8/EUC/SJIS) の編集とファイル名 |
| `make test-jwp` | ワードプロセッサ jwp: 編集・折り返し・禁則・スクロール・保存、SKK の変換・登録・学習・辞書の二分探索、乱数のキー列で差分描画が全画面描画と一致すること (Level 1/2、UTF-8/EUC/SJIS) |
| `make test-dhry` | Dhrystone: 最終値の検査 (Dhrystone 2.1 の正しい値)、時間付きの実行、`-clock` に比例すること |
| `make test-fuzix` | FUZIX: 起動、MBR/スワップの検出、シェル、ファイル、パイプ、`ps`、`df`、時計、`shutdown` / `halt`、再起動後のファイル、`hdb` (17 項目。`fuzix/images` がないときは skip) |
| `make test-hdmi` | FPGA の HDMI 文字画面: 端末の動作 (折り返し、スクロール、スクロール領域、行の挿入と削除、消去、色) を別に書いたモデルと 1 セルずつ比較 (88 項目)、TMDS の符号と直流平衡、**漢字**: 3 つの文字コードの復号、2 桁の文字の端末の動作を全画素で比較 (124 項目) |
| `make test-fpga` | FPGA の RTL を Verilator で動かす: FUZIX の起動・ログイン・コマンド・`shutdown`・再起動、SDSC カード、カードなし、NitrOS-9 Level 1 の起動と `dir /d0` (約 5 分。Verilator がなければ skip) |
| `make test-net` | OS-9 Net: telnet ログイン、日本語往復、`nc` によるダイヤルアウト、切断検出 (Level 1/2) |
| `make test-cross` | 参考実装 (タイマを命令数駆動に改造したもの) と**全命令のレジスタトレースを比較** (Level 1/2 とも 150 万命令一致) |

`test-diff` / `test-cross` は参考実装の sbc09 ソース (`REF_SBC09=../sbc09_shinji`) を使います。

## 参考実装との相違 (意図したもの)

* **CPU**: v09s.c / 参考実装の v09 エンジンにある次の誤りは MC6809 データシートに従って修正しています
  (`docs/CPU.md` に詳細): PSHU/PULU のビット 6 (S)、RTI の E ビット判定、BGE/BLT/BGT/BLE の N⊕V、
  SWI で積む CC の E、DAA の N/Z、TFR=6 / LBcc=5(6) サイクル、ADD/ADC の H、TST の V。
* **Level 1 の `clock` モジュール** (`os9/level1/clock.asm`): 参考実装のものには 2 つの不具合があり修正しています。
  (1) IRQ 入口で DP=0 にしていないため、タイマ割り込みのたびにユーザープロセスのデータページの `$48` / `$59`
  (`D.Slice` / `D.Tick`) を 1 減らし、コマンドの出力文字が -1 される (`Directory` → `Directnry`) ことがありました。
  (2) 毎分 1 回、不定アドレスに時刻 6 バイトを書き込み (IRQ 内で `FTime` の `ldx R$X,u`)、モジュールディレクトリが壊れました
  (`FTimeS` に分離)。再発防止のテストが `tests/os9_test.py` にあります。
* **`help` / `error` コマンド**: a09 は、モジュール名の後ろに `org 0` のデータ領域がある NitrOS-9 のソースを誤ってアセンブルし
  (ヘッダの実行オフセットとモジュールサイズが 18 バイトずれる)、`help` と打つとシステムが固まっていました。
  `tools/fix_org.py` でソースを並べ替えてからアセンブルしています。
* **PDisk ドライバ** (`os9/level*/pdisk.asm`): RBF のドライバは LSN 0 の先頭 `DD.SIZ` バイトをドライブテーブルに複写する決まりですが、
  同梱の PDisk はこれをしていませんでした。そのため RBF が `DD.BIT`=0 で無限ループし、`/d0` `/d1` へのファイル作成が固まっていました
  (読み出しだけは動いていた)。LSN 0 の複写を追加し、作成・削除・`makdir` が動きます。
* **Level 2 の `ioman`**: a09 で `bcs` が範囲外になる箇所を `lbcs` に直したコピー (`os9/level2/ioman.asm`) を使っています。
* **VRBF**: 名前の大文字小文字を区別しない、実際の OS-9 エラー番号、`SS.Opt`/`SS.Size` 等の対応、
  `Create` は既存ファイルでエラー (E$CEF)、ディレクトリはソート済み、日本語ファイル名。
* **タイマ**: ホストの実時間ではなく CPU サイクル駆動 (再現性のため。`-max` 以外は実時間にペース合わせ)。
  割り込み要求はラッチされ、CPU が受け付けるか `$8F` 書き込みで解除されます。

## 既知の制限

* `-max` は OS-9 の `F$Sleep 1` (時間を譲るだけ) のため OS-9 内の時間がホストより速く進みます。ネットワークを使うときは付けないでください。
* ビルド中の `Pass 2 errors` の行は、すべて `0` であることを確認してください。
* `jwp` が扱える文書は Level 1 で 9000 バイト、Level 2 で 26000 バイトまでです (docs/JWP.md)。

## ディレクトリ構成

```
src/        sim09 本体   m6809.c(CPUコア) machine.c(バス/MMU/I/O/タイマ/スケジューラ) console.c(端末・文字コード・スクリプト)
            net.c(OS-9 Net) vdisk.c(VRBF) dbg.c(トレース/モニタ) dis6809.c(逆アセンブラ) main.c
os9/        OS-9 の ROM / モジュール / コマンドのビルド (make で os9v1.rom, os9v2.rom ほか)
  level1/ level2/   sbc09 用モジュール (pty, pdisk, vrbf, clock, boot, sysgo ...)
  kanji/    日本語対応 SCF (行編集)、dir、kmode
  dhry/     Dhrystone 2.1 (dhry.c: C 言語 / CMOC、ビルド済みモジュール)
  jwp/      日本語ワードプロセッサ (jwp.c, skk.c: C 言語 / CMOC、genmsgs.py、ビルド済みモジュール)
  skk/      SKK 辞書ディスクを作る Makefile (SKK-JISYO.L → RBF イメージ)
  net/      OS-9 Net: npty ドライバ, n1..n3 デスクリプタ, nc, login
  nitros9-code/     NitrOS-9 ソース (必要部分)    OS9.dsk WORK.dsk  ディスクイメージ    v09.rom モニタ
fpga/tn9k/  Tang Nano 9K 用の FPGA 設計: rtl/ (SoC、UART、タイマ、SD カード)、vendor/ (6809 コアと PSRAM コントローラ)、sim/ (Verilator)、scripts/build.sh (Gowin IDE)
fuzix/      FUZIX の移植: platform-sim09 (カーネルのプラットフォーム部)、bootrom.s、mkdisk.py、build.sh、images/ (ブート ROM とディスクイメージ)
tools/      build_gcc6809.sh (FUZIX 用の gcc 4.6.4 を作る)、a09 (OS-9 対応アセンブラ), make9rom, os9mod, mkskkdic.py (SKK 辞書), mkrbf.py (RBF ディスクイメージ), install_cmoc.sh ほか
tests/      自己テスト一式
docs/       FPGA.md FUZIX.md CPU.md KANJI.md OS9NET.md JWP.md PORTING.md (jwp の機種依存) DHRYSTONE.md
```

## ライセンス

**ファイルによって異なります** (単一のライセンスではありません)。詳しくは [LICENSES.md](LICENSES.md) を見てください。

* このプロジェクトで書いたもの (シミュレータ `src/`、テスト、`jwp` `skk` `dhry` のソース、辞書とディスクのツール、文書): **MIT** ([LICENSES/MIT.txt](LICENSES/MIT.txt))
* sbc09 由来のもの (`tools/` のアセンブラなど、`os9/level1` `os9/level2`、ディスクイメージ、ROM): **GPL v2** ([LICENSES/GPL-2.0.txt](LICENSES/GPL-2.0.txt))
* ビルド済みの `jwp` `jwp-l2` `dhry` (CMOC の実行時ライブラリを含む): **GPL v3** ([LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt))
* NitrOS-9 のソース (`os9/nitros9-code` と、それを改造したもの): 上流は GPL v2 と申告していますが、ファイルには Microware の著作権表示や非商用の限定などがあり、**再配布の条件は確定できません**
* `fuzix/` : 移植のソースは FUZIX の派生なので **GPL v2** (自作のスクリプトは MIT)。ディスクイメージは FUZIX のカーネル (GPL v2)、ライブラリ (LGPL)、V7 由来のコマンド (Caldera の BSD 形式) を含む
* `fpga/tn9k/`: 自作の RTL は **MIT**、`vendor/mc6809` (Greg Miller の 6809 コア) は **BSD-3-Clause**、`vendor/psram` (Feng Zhou の PSRAM コントローラ) は **Apache-2.0**
* Dhrystone 本体 (`os9/dhry/dhry.c` の手続き): 原作者の著作物で、**ライセンス文がありません**

[CREDITS.md](CREDITS.md) に、参考にしたソースとそのクレジットがあります。
