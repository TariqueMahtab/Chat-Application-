// chat.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_CMD_LEN 1024
#define MAX_MSG_LEN 1024

void print_help() {
    printf("Available commands:\n");
    printf("help       - Display this help message\n");
    printf("myip       - Display this process's IP address\n");
    printf("myport     - Display the port on which this process is listening\n");
    printf("connect IP PORT - Connect to a peer\n");
    printf("exit       - Close the application\n");
}

char* get_my_ip() {
    static char ip[INET_ADDRSTRLEN];
    WSADATA wsaData;
    SOCKET temp_sock;
    struct sockaddr_in temp_addr;
    int addrlen = sizeof(temp_addr);

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return "0.0.0.0";

    temp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (temp_sock == INVALID_SOCKET) {
        WSACleanup();
        return "0.0.0.0";
    }

    temp_addr.sin_family = AF_INET;
    temp_addr.sin_addr.s_addr = inet_addr("8.8.8.8");
    temp_addr.sin_port = htons(53);

    connect(temp_sock, (SOCKADDR*)&temp_addr, sizeof(temp_addr));
    getsockname(temp_sock, (SOCKADDR*)&temp_addr, &addrlen);

    strcpy(ip, inet_ntoa(temp_addr.sin_addr));

    closesocket(temp_sock);
    WSACleanup();
    return ip;
}

DWORD WINAPI receive_messages(LPVOID socket_ptr) {
    SOCKET sock = *((SOCKET*)socket_ptr);
    char buffer[MAX_MSG_LEN];

    while (1) {
        int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) break;

        buffer[bytes_received] = '\0';
        printf("\n[Peer]: %s\n> ", buffer);
        fflush(stdout);
    }

    return 0;
}

void start_server(int port) {
    WSADATA wsaData;
    SOCKET server_sock, client_sock;
    struct sockaddr_in server, client;
    int c;
    char message[MAX_MSG_LEN];

    WSAStartup(MAKEWORD(2,2), &wsaData);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    bind(server_sock, (struct sockaddr *)&server, sizeof(server));
    listen(server_sock, 1);
    printf("Chat program started. Listening on port %d.\n> ", port);

    c = sizeof(struct sockaddr_in);
    client_sock = accept(server_sock, (struct sockaddr *)&client, &c);
    printf("Connection established with %s:%d\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));

    CreateThread(NULL, 0, receive_messages, &client_sock, 0, NULL);

    while (1) {
        printf("> ");
        fgets(message, sizeof(message), stdin);
        message[strcspn(message, "\n")] = 0;
        if (strcmp(message, "exit") == 0) break;
        send(client_sock, message, strlen(message), 0);
    }

    closesocket(client_sock);
    closesocket(server_sock);
    WSACleanup();
}

void connect_to_peer(char* ip, int port) {
    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server;
    char message[MAX_MSG_LEN];

    WSAStartup(MAKEWORD(2, 2), &wsaData);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_addr.s_addr = inet_addr(ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        printf("Connection failed to %s:%d\n", ip, port);
        closesocket(sock);
        WSACleanup();
        return;
    }

    printf("Connected to %s:%d\n", ip, port);

    CreateThread(NULL, 0, receive_messages, &sock, 0, NULL);

    while (1) {
        printf("> ");
        fgets(message, sizeof(message), stdin);
        message[strcspn(message, "\n")] = 0;
        if (strcmp(message, "exit") == 0) break;
        send(sock, message, strlen(message), 0);
    }

    closesocket(sock);
    WSACleanup();
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    char command[MAX_CMD_LEN];

    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)start_server, (LPVOID)(intptr_t)port, 0, NULL);

    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(command, MAX_CMD_LEN, stdin)) break;
        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "help") == 0) {
            print_help();
        } else if (strcmp(command, "myip") == 0) {
            printf("IP Address: %s\n", get_my_ip());
        } else if (strcmp(command, "myport") == 0) {
            printf("Listening on port: %d\n", port);
        } else if (strncmp(command, "connect ", 8) == 0) {
            char ip[INET_ADDRSTRLEN];
            int peer_port;
            if (sscanf(command + 8, "%s %d", ip, &peer_port) == 2) {
                connect_to_peer(ip, peer_port);
            } else {
                printf("Usage: connect <ip> <port>\n");
            }
        } else if (strcmp(command, "exit") == 0) {
            printf("Shutting down...\n");
            break;
        } else {
            printf("Unknown command. Type 'help' for available options.\n");
        }
    }

    return 0;
}
