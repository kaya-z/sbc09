# OS-9 で Dhrystone を動かす

シミュレータ上の OS-9 (Level 1 / Level 2) で、Dhrystone 2.1 のベンチマーク `dhry` が動きます。
ソースは `os9/dhry/dhry.c` (C 言語、CMOC でコンパイル)、ビルド済みのモジュールが `os9/level1/cmds/dhry` と
`os9/level2/cmds/dhry` に入っています。

## 使い方

```
OS9:chx /v0/cmds
OS9:dhry                10 秒以上 (OS-9 の時間で) 回して結果を出す
OS9:dhry -t 30          30 秒以上
OS9:dhry 5000           5000 回だけ回す (短い実行。最終値の検査と所要秒数を出す)
```

```sh
./sim09 -rom os9/os9v1.rom -0 os9/OS9.dsk -1 os9/WORK.dsk -v os9/level1        # 1.8432 MHz で実時間
./sim09 -max -clock 3686400 ...                                                # 3.6864 MHz 相当を全速で
```

出力の例 (Level 1、1.8432 MHz):

```
Dhrystone Benchmark, Version 2.1 (Language: C, CMOC on OS-9)

Final values of the variables used in the benchmark:
Int_Glob:            5
...
Dhrystone checks OK

Runs:                  2060
Time:                  10 s
Microseconds for one run through Dhrystone: 4854
Dhrystones per second: 206
VAX MIPS rating (Dhrystones/s / 1757): 0.11
```

最終値は Dhrystone 2.1 のソースに書かれている正しい値と照合し、1 つでも違えば `WRONG ...` と
`Dhrystone checks FAILED` を出します。

## 結果 (サイクル数による正確な値)

OS-9 の時計は 1 秒単位なので、`dhry` 自身の結果は長く回したときだけ正確です。シミュレータのサイクル数を使う
`tools/dhry_bench.py` は、`dhry 1000` と `dhry 4000` を別々に実行して CPU サイクルの差を取り (起動やシェルの分が消えます)、
1 回あたりのサイクル数を求めます。タイマ割り込みやスケジューラの分は含まれます。

```sh
tools/dhry_bench.py -1            # Level 1
tools/dhry_bench.py -2 --clock 2000000
```

| | 1 回あたり | 命令数 | サイクル/命令 | Dhrystones/s (1.8432 MHz) | DMIPS | DMIPS/MHz |
|---|---|---|---|---|---|---|
| Level 1 | 8766 サイクル | 1581 | 5.55 | 210.3 | 0.120 | 0.065 |
| Level 2 | 8852 サイクル | 1600 | 5.53 | 208.2 | 0.119 | 0.064 |

Dhrystones/s はクロックに比例します (2 MHz なら約 228、3.6864 MHz なら約 420)。DMIPS は Dhrystones/s を 1757 (VAX 11/780 の値) で
割ったものです。

読み方の注意:

* **コンパイラの出来が結果になります。** これは CMOC が出した 6809 のコードの速さで、手で書いたアセンブラの速さではありません。
  16 ビットの `int`、文字列関数 (`strcpy` `strcmp` は CMOC のライブラリ)、構造体の代入 (`memcpy` 相当) の効率が効きます。
  手書きなら数倍になり得ます。他のコンパイラ (たとえば gcc や ACK の 6809 版) の数字と並べるときは、コンパイラと最適化を明記してください。
* **サイクルは E クロックです。** MC6809 のサイクル数 (データシートの値) で数えているので、「クロック」は水晶の周波数ではなく
  バスのサイクルの周波数です ([CPU.md](CPU.md))。1 MHz の E クロックは、4 MHz の水晶に相当します。
* **OS-9 の上で動いています。** 割り込みで時計を進め、時間割り (スケジューラ) も回っているので、裸のプログラムより遅くなります。
  Level 2 の方がわずかに遅い (約 1%) のは、システム側の処理 (タイマ割り込みなど) の差によると思われます (原因は調べていません)。
* Dhrystone は小さなプログラムで、メモリやディスクの速さはほとんど測れません。

## 時間のはかり方と、OS-9 の時計

* OS-9 の `F$Time` は 1 秒単位です。`dhry` は、短い塊 (10 回) ずつ回し、**秒が変わったところで始め、秒が変わったところで終える**ので、
  測る時間はちょうど整数秒で、誤差は塊 1 つ分 (約 0.05 秒、10 秒で 0.5% 未満) です。
* シミュレータの RTC (タイマの `$04` コマンド) は、以前は呼ばれた時点の**ホストの時刻**を返していました。そのため全速 (`-max`) で
  動かすと OS-9 の時計がほとんど進まず、時間を測れませんでした。今は**起動時のホストの時刻 + エミュレートした経過時間**
  (実行したサイクル数 ÷ `-clock`) を返します。OS-9 の時計は、タイマ割り込みや `sleep` と同じ基準で進み、実時間で動かすときは
  従来どおりホストの時計とほぼ一致します。`date` の日付は起動時のホストの日付です。`-icount` の照合用の実行では固定の時刻です。
* したがって `-max` でも、`-clock` を変えても、`dhry` の結果はエミュレートした機械の値になります。

## ビルド

```sh
tools/install_cmoc.sh $HOME/cmoc                 # CMOC と LWTOOLS (最初の 1 回、ネットワークが要ります)
make -C os9/dhry PATH=$HOME/cmoc/bin:$PATH       # dhry を作る
make -C os9 cmds                                 # level1/cmds, level2/cmds にコピー
```

モジュール名は出力ファイル名なので、`cmoc -o dhry dhry.c` のようにパスなしで作ります ([JWP.md](JWP.md) の注意と同じ)。
CMOC には `malloc` がないので、元のプログラムが `malloc` で取る 2 つのレコードは静的な領域にしてあります。
プログラムは 8KB ほどで、Level 1 でも余裕で動きます。

## 元のプログラムと、結果を公表するときの注意

Reinhold P. Weicker, "Dhrystone: A Synthetic Systems Programming Benchmark", CACM 27(10), 1984 の Version 2.1 (C) です。
手続きと関数 (`Proc_1`..`Proc_8`、`Func_1`..`Func_3`) の名前と構造は元のままで、元は `dhry_1.c` / `dhry_2.c` に分かれていたものを
1 つのファイルにし、ループの本体を `dhry_loop()` に出して塊ごとに回せるようにしました。時間の測り方は新しく書いたものです。
書き起こすときに元のファイルは見ていませんが、netlib の原本と突き合わせたところ、手続きの本体は空白と括弧と構造体代入の書き方を除いて同じでした (97〜100%)。実質的に原本の複製です。最終値は Dhrystone 2.1 が正しいとする値と一致します。

原作者の測定の約束 (ground rules、netlib の配布物に書かれています) は次のとおりで、**この版は最初の 2 つを満たしません**。
結果を公表するときは、そのことを明記してください。

* 別々にコンパイルする (`dhry_1.c` と `dhry_2.c`)。→ この版は 1 ファイル
* 手続きを統合 (インライン化) しない。→ CMOC は手続きを統合しませんが、この版は元の構造を変えているので、同じとは言えません
* それ以外のコンパイラの最適化は構わないが、明記する。→ CMOC の最適化レベル (既定の `-O2`) で作っています
* 既定の結果は `register` 宣言なしのもの。→ `register` は使っていません (`REG` は空)

**ライセンス**: 原本にはライセンス文がなく (netlib の配布物を確認しました)、再配布の条件は示されていません。
時間の計測と OS-9 の呼び出しの部分だけが MIT です。[LICENSES.md](../LICENSES.md) の 4.2 を見てください。
