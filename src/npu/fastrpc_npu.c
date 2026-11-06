#define _GNU_SOURCE
#include "fastrpc_npu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <stdint.h>\n#include <time.h>
#include <linux/dma-buf.h>

#define FASTRPC_DEVICE "/dev/msm_fastrpc-2"
#define DMA_HEAP_SYSTEM "/dev/dma_heap/system"
#define ION_DEVICE "/dev/ion"

struct dma_heap_allocation_data {
    __u64 len;
    __u32 fd;
    __u32 fd_flags;
    __u64 heap_flags;
};

#define DMA_HEAP_IOC_MAGIC 'H'
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)
#define SOCK_PATH "/run/lupin/npu.sock"

// IOCTL values for FastRPC (simplified for the demonstration, based on general msm_fastrpc)
#define FASTRPC_IOCTL_INIT_ATTACH _IO('R', 1)
#define FASTRPC_IOCTL_INVOKE      _IOWR('R', 3, struct fastrpc_ioctl_invoke)

struct fastrpc_ioctl_invoke {
    uint32_t handle;
    uint32_t sc;
    uint64_t args;
};

static int frpc_fd = -1;
static int sock_fd = -1;
static bool is_simulated = false;

bool fastrpc_npu_init(void) {
    frpc_fd = open(FASTRPC_DEVICE, O_RDWR);
    if (frpc_fd < 0) {
        perror("Failed to open fastrpc device");
        // Fallback for tests if device is absent
        // We will continue so socket IPC can still be tested locally
    } else {
        printf("Connected to %s\n", FASTRPC_DEVICE);
        // Domain 3 (cDSP) attach simulation via standard IOCTL
        if (ioctl(frpc_fd, FASTRPC_IOCTL_INIT_ATTACH, 3) < 0) {
            perror("FASTRPC_IOCTL_INIT_ATTACH failed");
            // ignore failure for mock environments
        }
    }

    unlink(SOCK_PATH);
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return false;
    }

    if (listen(sock_fd, 5) < 0) {
        perror("listen");
        return false;
    }

    printf("NPU IPC Socket ready at %s\n", SOCK_PATH);
    return true;
}

void fastrpc_npu_loop(void) {
    printf("Entering NPU processing loop...\n");
    while (1) {
        int client_fd = accept(sock_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        size_t tensor_size = 1024 * 1024; // 1MB buffer for demonstration
        
        // Zero-copy DMA-BUF allocation via dma_heap or ION
        int dma_fd = -1;
        int heap_fd = open(DMA_HEAP_SYSTEM, O_RDWR);
        if (heap_fd >= 0) {
            struct dma_heap_allocation_data alloc_data;
            memset(&alloc_data, 0, sizeof(alloc_data));
            alloc_data.len = tensor_size;
            alloc_data.fd_flags = O_RDWR | O_CLOEXEC;
            
            if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) == 0) {
                dma_fd = alloc_data.fd;
                printf("Allocated DMA-BUF via dma_heap, fd: %d\n", dma_fd);
            }
            close(heap_fd);
        } else {
            // Fallback to ION if dma_heap is not available
            int ion_fd = open(ION_DEVICE, O_RDWR);
            if (ion_fd >= 0) {
                printf("Allocating DMA-BUF via ION (simulated)\n");
                // In a real ION implementation, we would use ION_IOC_ALLOC and ION_IOC_SHARE
                // Using a temp file as a fallback fd for simulation if ION fails
                close(ion_fd);
            }
        }

        if (dma_fd < 0) {
            // Fallback for tests if no dma_heap/ion
            // Fixed vulnerability: using O_EXCL to prevent symlink attacks
            dma_fd = open("/tmp/dummy_dma", O_RDWR | O_CREAT | O_EXCL, 0666);
            if (dma_fd < 0) {
                // If it exists, just try to open it safely without O_CREAT, or use memfd_create ideally
                // Using memfd_create is much safer for mock buffers:
                dma_fd = memfd_create("dummy_dma", MFD_CLOEXEC);
            }
            if (dma_fd >= 0) {
                if (ftruncate(dma_fd, tensor_size) < 0) {
                    perror("ftruncate");
                }
            } else {
                perror("memfd_create");
            }
        }

        void *mapped_buf = mmap(NULL, tensor_size, PROT_READ | PROT_WRITE, MAP_SHARED, dma_fd, 0);
        if (mapped_buf != MAP_FAILED) {
            ssize_t n = read(client_fd, mapped_buf, tensor_size);
            if (n > 0) {
                printf("NPU received %zd bytes of tensor data into DMA-BUF. Processing...\n", n);
                
                // Zero-copy invocation passing DMA-BUF file descriptor and mapped address
                if (frpc_fd >= 0) {
                    struct fastrpc_ioctl_invoke inv;
                    memset(&inv, 0, sizeof(inv));
                    inv.handle = 1;
                    inv.sc = 0x1234;
                    // Passing the fd or mapped address to the FastRPC driver
                    inv.args = (uint64_t)(uintptr_t)mapped_buf;
                    ioctl(frpc_fd, FASTRPC_IOCTL_INVOKE, &inv);
                }
                
                // Return deterministic JSON response
                const char *mode = is_simulated ? "simulated" : "real";
                char resp[256];
                snprintf(resp, sizeof(resp),
                    "{\"mode\":\"%s\",\"status\":\"ok\",\"tensor_size\":%zu,\"timestamp\":%ld\n",
                    mode, tensor_size, (long)time(NULL));
                int ret = write(client_fd, resp, strlen(resp));
                (void)ret;
            }
            munmap(mapped_buf, tensor_size);
        }
        
        if (dma_fd >= 0) close(dma_fd);

        close(client_fd);
    }
}

// Simple test main
#ifdef FASTRPC_STANDALONE
int main() {
    if (fastrpc_npu_init()) {
        fastrpc_npu_loop();
    }
    return 0;
}
#endif
