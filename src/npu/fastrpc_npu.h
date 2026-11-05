#ifndef FASTRPC_NPU_H
#define FASTRPC_NPU_H

#include <stdbool.h>

// Initialize FastRPC connection to domain 3 (cDSP) and create Unix IPC socket /tmp/lupin_npu.sock
bool fastrpc_npu_init(void);

// Run the FastRPC NPU engine processing loop
void fastrpc_npu_loop(void);

#endif // FASTRPC_NPU_H
