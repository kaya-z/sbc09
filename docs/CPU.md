# CPU コア (`src/m6809.c`)

## 参考にしたもの

| 参考 | 使った点 |
|---|---|
| sbc09 `v09s.c` (Lennart Benschop, Jens Diemer, Johann Klasek; GPL v2) | 命令仕様 (オペコード表、フラグ、TFR/EXG の 8/16 ビット混在、未定義命令、サイクル数)。`tests/diff_v09s.c` が同ファイルを取り込んで突き合わせる |
| MAME `m6809.cpp` (Nathan Woods, Tim Lindner ほか; BSD-3-Clause) | クロック／サイクル／CPU 状態の考え方 (下表)。コードは流用していない |
| sbc09 `engine.c` / `io.c` (Shinji Kono ほか) | OS-9 を動かすためのメモリマップ、MMU、I/O レジスタ規約、割り込みの上げ方 |

### MAME との対応

| MAME | 本実装 |
|---|---|
| `m_icount` と `execute_run()` | `cpu->icount` / `m6809_run(c, cycles)`: 予算を渡し、命令ごとに消費サイクルを引く |
| 命令ごとの `eat()` による経過サイクル | 命令ごとに表どおりのサイクルを `cyc` に積算し、`total_cycles` (絶対クロック) を進める。周辺機器 (タイマ等) はこの絶対クロックで動く |
| `CWAI_IN_PROGRESS` / `SYNC_IN_PROGRESS` | `enum m6809_state { RUN, SYNC, CWAI, HALTED, RESETTING }` |
| `set_input_line(IRQ/FIRQ/NMI/HALT/RESET)` | `m6809_set_line()`。IRQ/FIRQ はレベル、NMI は立ち上がりエッジでラッチ |
| LDS 後に NMI を有効にする (`m_lds_encountered`) | `nmi_armed`。LDS / LEAS / TFR・EXG で S に書くまで NMI は保留 |
| LIC 出力 | `cpu->lic` (命令境界で 1) |
| スケジューラが次のイベントまでの予算を渡す | `machine_run()`: 次のタイマ満了までの予算で `m6809_run()` を呼び、使ったサイクルだけ周辺機器を進める。CPU が CWAI/SYNC で待っていて割り込みもなければ時間を次のイベントまで飛ばす (アイドルスキップ) |

## 状態機械

* `m6809_step()` は「1 命令」「割り込み受付」「待機 1 サイクル」のどれかを実行して消費サイクルを返す。
* 優先順位: RESET > HALT > NMI > FIRQ (F=0) > IRQ (I=0)。
* 割り込み受付: IRQ / NMI は 19 サイクル (全レジスタを積んで E=1)、FIRQ は 10 サイクル (PC, CC のみ、E=0)。
* **CWAI**: `CC &= imm`、E=1 で全レジスタを積み (20 サイクル)、待機。割り込みが来ると積み直さずにベクタへ (4 サイクル)。
* **SYNC**: 4 サイクルで待機に入り、IRQ/FIRQ/NMI のどれかが*アサートされれば*マスクされていても起床する。
  マスクされていなければそのまま割り込みを受け付け、マスクされていれば次の命令に進む。
* HALT 線: 命令境界で CPU を止める。RESET 線: 1 サイクルずつ待機し、解除後に `$FFFE` から再開。

## サイクル数

MC6809E データシート/プログラミングマニュアルの値です。

* 基本値 (命令ごと) + インデックスのポストバイト加算 (`,R`=0, `n5,R`/`n8,R`/`A,R`/`B,R`/`n8,PCR`=1, `,R+`/`,-R`=2,
  `,R++`/`,--R`=3, `n16,R`/`D,R`=4, `n16,PCR`=5, 間接はさらに +3、`[n16]`=5)
* `$10` / `$11` ページ命令は基本値 + 1 (LDY # = 4, CMPD # = 5, SWI2/3 = 20, LBcc = 5 (成立時 6))
* MUL=11, SWI=19, RTI=6 (E=1 なら 15), CWAI=20, TFR=6, EXG=8, PSHS/PULS = 5 + バイト数, LBSR=9, BSR/JSR=7/7/8, ABX=3

`tests/cpu_test.c` に主な命令のサイクル数のテストがあります。

## v09s.c / 参考実装 (engine.c) との違い

v09s.c や engine.c には MC6809 データシートと食い違う箇所があり、本実装は**データシートに従っています**
(`tests/diff_v09s.c` は下記を既知の相違として除外し、他の全命令・全ページで一致を確認しています)。

| 項目 | v09s.c / engine.c | 本実装 |
|---|---|---|
| PSHU / PULU のビット 6 | U を積む/戻す | S |
| RTI | 積まれた CC ではなく*現在の* CC の E を見る | 取り出した CC の E |
| BGE / BLT / BGT / BLE (v09s) | `N & 8` と `V & 2` を揃えずに XOR (誤り) | N⊕V |
| SWI | E を立てる前の CC を積む | E=1 で積む |
| DAA | N/Z を更新しない簡略版 | 標準の補正 (N, Z, C) |
| TFR | 7 サイクル | 6 サイクル |
| LBcc | 常に 6 | 5 (成立時 6) |
| `[n16]` 間接 | +1 | データシートどおり +5 |
| ADD/ADC の H、TST の V (engine.c) | H を更新しない / V を消さない | H を更新 / V=0 |
| ASL/ASR の H | engine.c は ADD 同様 / ビット 4 のコピー | 不定なので変更しない |
| 不正オペコード | NOP 相当 (オペランドを読むことも) | 1 サイクルの NOP とみなし `illegal_count` を加算 (`on_illegal` フック) |

さらに `tests/crosscheck/crosscheck.sh` は、参考実装のシミュレータ (タイマを命令数駆動に、上の誤りを直したコピー) と
本実装に同じ OS-9 ROM・同じ入力を与え、**全命令のレジスタ (PC, A, B, X, Y, U, S, CC, DP) を比較**します。
Level 1 / Level 2 とも 150 万命令 (起動、タイマ割り込み、コマンド入力、`dir -e`、`mdir -e`) で一致します。
