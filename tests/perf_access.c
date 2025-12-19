#include <stdio.h>
#include <riscv-pk/encoding.h>
#include "marchid.h"
#include "perf_define.h"
#include <stdint.h>

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

  // 启用性能计数器
  SetPfcEnable(1);
  uint64_t ptag = 0x1234567;
  SetProcTag(ptag);
  SetCounterLevel("3");

  uint64_t marchid = read_csr(marchid);
  const char* march = get_march(marchid);
  printf("Hello world from core 0, a %s\n", march);
  
  // 再次读取 counter 值
  ReadCounter8(counters, 0);
  printf("New Counters Value: 0x%lx, 0x%lx\n", counters[0], counters[1]); 

  // 输出此时的 ProcTag 和 Max Privilege Level
  Get_ProcTag_Priv(tag, priv);
  printf("Proc Tag: 0x%lx, Max Privilege Level: 0x%lx\n", tag, priv);
  
  return 0;
}
