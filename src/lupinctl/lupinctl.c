#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#define SOCKET_PATH "/run/lupin/control.sock"

void print_usage() {
    printf("Lupin Control (lupinctl) - Sovereign Control Plane\n");
    printf("Usage:\n");
    printf("  lupinctl status\n");
    printf("  lupinctl power <profile>\n");
    printf("  lupinctl net <up|down>\n");
    printf("  lupinctl hud <on|off>\n");
    printf("  lupinctl npu <start|stop|status>\n");
    printf("  lupinctl thermal <status>\n");
}

int send_command(const char *cmd) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (errno == ENOENT || errno == ECONNREFUSED) {
            fprintf(stderr, "Error: Daemon is not running or socket %s not found.\n", SOCKET_PATH);
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
