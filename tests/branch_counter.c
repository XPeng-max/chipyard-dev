#include "marchid.h"
#include "perf_define.h"
#include <riscv-pk/encoding.h>
#include <stdint.h>
#include <stdio.h>

uint64_t counters[64];
uint64_t ptag = 0x1234567;

#define NOINLINE __attribute__((noinline))

// 1) Always taken
NOINLINE int bench_always_taken(int N) {
  volatile int acc = 0;
  for (int i = 0; i < N; ++i) {
    volatile int v = i; // 防止编译器常量折叠
    if (v != -1)
      acc++;
    else
      acc--;
  }
  return acc;
}

// 2) Always not taken
NOINLINE int bench_always_not_taken(int N) {
  volatile int acc = 0;
  for (int i = 0; i < N; ++i) {
    volatile int v = i;
    if (v == -1)
      acc++;
    else
      acc--;
  }
  return acc;
}

// 4) Periodic: 每 3 次命中一次
NOINLINE int bench_periodic3(int N) {
  volatile int acc = 0;
  for (int i = 0; i < N; ++i) {
    if (i % 3 == 0)
      acc++;
    else
      acc--;
  }
  return acc;
}

// 5) 伪随机分支（难以被静态/简单历史预测）
static uint64_t lcg_state = 1;
static inline uint64_t lcg_next() {
  lcg_state = lcg_state * 6364136223846793005ULL + 1ULL;
  return lcg_state;
}
NOINLINE int bench_random(int N) {
  volatile int acc = 0;
  for (int i = 0; i < N; ++i) {
    if ((lcg_next() & 256ULL) == 0)
      acc++;
    else
      acc--;
  }
  return acc;
}

// 5) Indirect calls (JALR via function pointer) + 目标切换
NOINLINE int foo(int x) { return x + 1; }
NOINLINE int bar(int x) { return x - 1; }
NOINLINE int bench_indirect_calls(int N) {
  int (*fp)(int) = foo;
  volatile int acc = 0;
  for (int i = 0; i < N; ++i) {
    acc += fp(acc);
    // 每 8 次切换一次目标，锻炼 BTB/间接预测器
    if ((i & 7) == 7)
      fp = (fp == foo) ? bar : foo;
  }
  return acc;
}

// 6) 深层调用链（测试 RAS）
NOINLINE int leaf(int x) { return x + 1; }
NOINLINE int mid2(int x) { return leaf(x) + 1; }
NOINLINE int mid1(int x) { return mid2(x) + leaf(1); }
NOINLINE int bench_call_chain(int N) {
  volatile int acc = 0;
  for (int i = 0; i < N; ++i) {
    // 使得 leaf, mid2 函数的 ret 指令有多种返回地址
    acc += mid1(acc) + mid2(acc) + leaf(acc);
  }
  return acc;
}

// 7) 递归（防尾调用优化，形成真实返回栈弹栈）
NOINLINE int bench_recursion_inner(volatile int depth) {
  if (depth <= 0)
    return 1;
  return 1 + bench_recursion_inner(depth - 1);
}
NOINLINE int bench_recursion(int trips, int depth) {
  volatile int acc = 0;
  for (int i = 0; i < trips; ++i)
    acc += bench_recursion_inner(depth);
  return acc;
}

int main(void) {
  RESET_COUNTER();

  // 启用性能计数器
  SetPfcEnable(1);
  SetProcTag(ptag);
  SetCounterLevel("3");

  int N = 1000;

  // 测试 RAS
  bench_call_chain(N);
  // 40 层的递归调用会超过 medium boom 中 32 大小的
  // RAS，产生少量返回地址预测错误
  bench_recursion(N / 40, 40);

  ReadCounter16(&counters[0], 0);
  ReadCounter16(&counters[16], 16);

  uint64_t com_ret = counters[10];
  uint64_t misp_ret = counters[15];
  uint64_t inst_from_f1 = counters[16];
  uint64_t inst_from_f2 = counters[17];
  uint64_t inst_from_f3 = counters[18];
  uint64_t inst_from_bsrcc = counters[19];
  printf("[RAS] Committed Ret: %ld, Mispredicted Ret: %ld(mispredicted number "
         "should be small)\n",
         com_ret, misp_ret);
  printf("[RAS] inst from f1 pred: %ld, from f2 pred: %ld, from f3 pred: %ld, "
         "from bsrcc: %ld(many from f3 because of ret?)\n",
         inst_from_f1, inst_from_f2, inst_from_f3, inst_from_bsrcc);

  RESET_COUNTER();
  // 测试间接分支预测
  bench_indirect_calls(N);

  ReadCounter16(&counters[0], 0);
  ReadCounter16(&counters[16], 16);

  uint64_t com_jalr = counters[9];
  uint64_t misp_jalr = counters[14];
  inst_from_f1 = counters[16];
  inst_from_f2 = counters[17];
  inst_from_f3 = counters[18];
  inst_from_bsrcc = counters[19];
  printf("[JALR] Committed JALR: %ld, Mispredicted JALR: %ld(mispredicted "
         "number should be about 1/8)\n",
         com_jalr, misp_jalr);
  printf("[JALR] inst from f1 pred: %ld, from f2 pred: %ld, from f3 pred: %ld, "
         "from bsrcc: %ld(many from bsrcc because of misprediction?)\n",
         inst_from_f1, inst_from_f2, inst_from_f3, inst_from_bsrcc);

  RESET_COUNTER();
  // 测试条件分支预测
  bench_always_taken(N);
  bench_always_not_taken(N);

  ReadCounter16(&counters[0], 0);
  ReadCounter16(&counters[16], 16);

  uint64_t com_br = counters[8];
  uint64_t misp_br = counters[13];
  inst_from_f1 = counters[16];
  inst_from_f2 = counters[17];
  inst_from_f3 = counters[18];
  inst_from_bsrcc = counters[19];
  printf("[Simple BR] Committed BR: %ld, Mispredicted BR: %ld(mispredicted "
         "number should be small)\n",
         com_br, misp_br);
  printf("[Simple BR] inst from f1 pred: %ld, from f2 pred: %ld, from f3 pred: "
         "%ld, from bsrcc: %ld(mainly from f1?)\n",
         inst_from_f1, inst_from_f2, inst_from_f3, inst_from_bsrcc);

  RESET_COUNTER();
  // 测试条件分支预测
  bench_periodic3(N);

  ReadCounter16(&counters[0], 0);
  ReadCounter16(&counters[16], 16);

  com_br = counters[8];
  misp_br = counters[13];
  inst_from_f1 = counters[16];
  inst_from_f2 = counters[17];
  inst_from_f3 = counters[18];
  inst_from_bsrcc = counters[19];
  // TAGE 应该能预测这种周期性行为
  printf("[Periodic3 BR] Committed BR: %ld, Mispredicted BR: %ld(mispredicted "
         "number should be about small)\n",
         com_br, misp_br);
  printf("[Periodic3 BR] inst from f1 pred: %ld, from f2 pred: %ld, from f3 "
         "pred: %ld, from bsrcc: %ld (many from f3 TAGE?)\n",
         inst_from_f1, inst_from_f2, inst_from_f3, inst_from_bsrcc);

  RESET_COUNTER();
  // 测试条件分支预测
  bench_random(N);
  ReadCounter16(&counters[0], 0);
  ReadCounter16(&counters[16], 16);

  com_br = counters[8];
  misp_br = counters[13];
  inst_from_f1 = counters[16];
  inst_from_f2 = counters[17];
  inst_from_f3 = counters[18];
  inst_from_bsrcc = counters[19];
  printf("[Random BR] Committed BR: %ld, Mispredicted BR: %ld(mispredicted "
         "number should be about half)\n",
         com_br, misp_br);
  printf("[Random BR] inst from f1 pred: %ld, from f2 pred: %ld, from f3 pred: "
         "%ld, from bsrcc: %ld(many from bsrcc because of misprediction?)\n",
         inst_from_f1, inst_from_f2, inst_from_f3, inst_from_bsrcc);
  return 0;
}
