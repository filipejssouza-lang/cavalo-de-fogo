#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#define SOCKET_PATH "/run/lupin/control.sock"
#define HUD_SOCKET_PATH "/run/lupin/hud.sock"

static const char *get_hud_socket(void) {
    const char *env = getenv("LUPIN_HUD_SOCKET");
    if (env && strlen(env) > 0) return env;
    if (access("/run/lupin/hud.sock", F_OK) == 0) {
        return "/run/lupin/hud.sock";
    }
    return "/tmp/lupin/hud.sock";
}

void print_usage(void) {
    printf("Lupin Control (lupinctl) - Sovereign Control Plane v6\n");
    printf("Usage:\n");
    printf("  lupinctl status [--json]\n");
    printf("  lupinctl power <idle|balanced|burst>\n");
    printf("  lupinctl net <up|down|status>\n");
    printf("  lupinctl hud <next|prev|status|dim|on>\n");
    printf("  lupinctl npu <start|stop|status>\n");
    printf("  lupinctl thermal <status>\n");
    printf("  lupinctl lora <load|list|unload> [args...]\n");
}

int send_command(const char *cmd) {
    const char *target_socket = SOCKET_PATH;
    
    if (strncmp(cmd, "hud ", 4) == 0) {
        target_socket = get_hud_socket();
    }

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, target_socket, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (errno == ENOENT || errno == ECONNREFUSED) {
            fprintf(stderr, "Error: Daemon is not running or socket %s not found.\n", target_socket);
        } else {
            perror("connect");
        }
        close(sock);
        return -1;
    }

    if (send(sock, cmd, strlen(cmd), 0) < 0) {
        perror("send");
        close(sock);
        return -1;
    }

    char buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
    }

    close(sock);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    char command_buffer[256];
    command_buffer[0] = '\0';
    for (int i = 1; i < argc; i++) {
        strncat(command_buffer, argv[i], sizeof(command_buffer) - strlen(command_buffer) - 1);
        if (i < argc - 1) {
            strncat(command_buffer, " ", sizeof(command_buffer) - strlen(command_buffer) - 1);
        }
    }

    return send_command(command_buffer) == 0 ? 0 : 1;
}
