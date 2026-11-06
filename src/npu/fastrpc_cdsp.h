#ifndef FASTRPC_CDSP_H
#define FASTRPC_CDSP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FASTRPC_DOMAIN_CDSP 3

// Initialize the FastRPC cDSP interface
// Returns true on success, false on failure
bool fastrpc_cdsp_init(void);

// Clean up the FastRPC cDSP interface
void fastrpc_cdsp_cleanup(void);

// Allocate a zero-copy DMA-BUF buffer for FastRPC
// size: size of the buffer to allocate
// Returns pointer to mapped memory, or NULL on failure
// Also provides the file descriptor via fd_out
void* fastrpc_cdsp_alloc_buffer(size_t size, int* fd_out);

// Free a zero-copy DMA-BUF buffer
void fastrpc_cdsp_free_buffer(void* ptr, size_t size, int fd);

// Execute a remote procedure call on the cDSP
// This is a simplified interface for inference invocation
bool fastrpc_cdsp_execute_inference(void* input_buf, size_t input_size,
                                    void* output_buf, size_t output_size);

#endif // FASTRPC_CDSP_H
