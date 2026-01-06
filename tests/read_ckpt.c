#include "marchid.h"
#include "perf_define.h"
#include <riscv-pk/encoding.h>
#include <stdint.h>
#include <stdio.h>

extern uint64_t __sampleFucAddr;

void sample_func();
void read_counters();
void init_start(uint64_t max_inst, uint64_t warmup_inst);
void print_out();

// 重要！！
// 测试前需要 boom 的 core.scala 的 `sampleValid` 和 `warmupValid` 信号生成逻辑
// 保证这俩信号在 M 态下也能生效，否则无法触发采样异常

int main(void) {
  RESET_COUNTER();

  // 读取初始 counter 值
  uint64_t counters[8];
  ReadCounter8(counters, 0);
  printf("Initial Counters Value: 0x%lx, 0x%lx\n", counters[0], counters[1]);

  // 读取初始的 ProcTag 和 Max Privilege Level
  uint64_t tag, priv;
  Get_ProcTag_Priv(tag, priv);
  printf("Proc Tag: 0x%lx, Max Privilege Level: 0x%lx\n", tag, priv);

  uint64_t maxevent = 1000, warmupinst = 1000, eventsel = 0;
  init_start(maxevent, warmupinst);

  uint64_t marchid = read_csr(marchid);
  const char *march = get_march(marchid);
  for (int i = 0; i < 2; ++i) {
    printf("Hello world from main core 0, a %s\n", march);
  }
  // 再次读取 counter 值
  ReadCounter8(counters, 0);
  printf("New Counters Value: 0x%lx, 0x%lx\n", counters[0], counters[1]);

  // 输出此时的 ProcTag 和 Max Privilege Level
  Get_ProcTag_Priv(tag, priv);
  printf("Proc Tag: 0x%lx, Max Privilege Level: 0x%lx\n", tag, priv);

  print_out();

  return 0;
}

uint64_t placefolds[1024];
uint64_t tempStackMem[4096];
uint64_t hpcounters[64];
uint64_t procTag = 0x1234567;
uint64_t maxevent = 1000, warmupinst = 1000, eventsel = 0, maxperiod = 0;
extern uint64_t __sampleFucAddr;

uint64_t exitpc = 0;
unsigned int logfile;
char str[256];

uint64_t startcycle = 0, endcycle = 0;
uint64_t startinst = 0, endinst = 0;

void init_start(uint64_t max_inst, uint64_t warmup_inst) {
  printf("warmup: %ld, maxinst: %ld\n", warmup_inst, max_inst);

  Save_Basic_Regs();
  tempStackMem[32] = (uint64_t)&tempStackMem[2048]; // set sp
  tempStackMem[33] = (uint64_t)&tempStackMem[2560]; // set s0 = sp + 512*8
  printf("sample function addr: 0x%lx, sp: 0x%lx, s0: 0x%lx\n",
         (uint64_t)&__sampleFucAddr, tempStackMem[32], tempStackMem[33]);
  startcycle = read_csr_cycle();
  startinst = read_csr_instret();

  maxevent = max_inst, warmupinst = warmup_inst, eventsel = 0;
  SetCounterLevel("3"); // verilator 测试时程序运行在 M 态
  RESET_COUNTER();
  SetPfcEnable(1);
  SetSampleBaseInfo(procTag, &__sampleFucAddr);
  SetSampleCtrlReg(maxevent, warmupinst, eventsel);
}

void sample_func() {
  asm volatile("__sampleFucAddr: ");
  Save_ALLIntRegs();
  Load_Basic_Regs();

  GetExitPC(exitpc);
  SetTempReg(exitpc, 0);

  endcycle = read_csr_cycle();
  endinst = read_csr_instret();
  // printf 的实现好像是不可重入的，这里使用 printf 会卡死掉
  // sprintf(str,
  //         "{\"type\": \"max_inst\", \"cycles\": %ld, \"inst\": "
  //         "%ld}\n",
  //         endcycle - startcycle, endinst - startinst);
  // printf("%s", str);
  //
  read_counters();

  // if(sampleHapTimes >= maxperiod) {
  //     exit(0);
  // }

  warmupinst = 0;
  maxevent = 0;
  // 不再开启 counter 计数，方便后续校验
  // RESET_COUNTER();
  // SetSampleBaseInfo(procTag, &__sampleFucAddr);
  SetPfcEnable(1);
  SetSampleCtrlReg(maxevent, warmupinst, eventsel);
  Load_ALLIntRegs();
  JmpTempReg(0);
}

void read_counters() {
  ReadCounter16(&hpcounters[0], 0);
  ReadCounter16(&hpcounters[16], 16);
  ReadCounter16(&hpcounters[32], 32);
  ReadCounter16(&hpcounters[48], 48);

  // for (int n = 0; n < 64; n++) {
  //   sprintf(str, "{\"type\": \"event %2d\", \"value\": %lu}\n", n,
  //           hpcounters[n]);
  //   printf("%s", str);
  // }
}

void print_out() {
  printf("{\"type\": \"max_inst\", \"cycles\": %ld, \"inst\": "
         "%ld}\n",
         endcycle - startcycle, endinst - startinst);
  for (int n = 0; n < 64; n++) {
    printf("{\"type\": \"event %2d\", \"value\": %lu}\n", n,
            hpcounters[n]);
  }
}