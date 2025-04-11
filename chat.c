// chat_v2.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_CMD_LEN 1024
#define MAX_MSG_LEN 1024
#define MAX_PEERS 10

typedef struct {
    int id;
    SOCKET socket;
    char ip[INET_ADDRSTRLEN];
    int port;
    int active;
} Peer;

Peer peer_list[MAX_PEERS];
CRITICAL_SECTION peer_list_lock;

int add_peer(SOCKET sock, const char* ip, int port) {
    EnterCriticalSection(&peer_list_lock);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!peer_list[i].active) {
            peer_list[i].id = i + 1;
            peer_list[i].socket = sock;
            strncpy(peer_list[i].ip, ip, INET_ADDRSTRLEN);
            peer_list[i].port = port;
            peer_list[i].active = 1;
            LeaveCriticalSection(&peer_list_lock);
            return i + 1;
        }
    }
    LeaveCriticalSection(&peer_list_lock);
    return -1;
}

void remove_peer(int id) {
    EnterCriticalSection(&peer_list_lock);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peer_list[i].id == id) {
            if (!peer_list[i].active) {
                printf("Error: Connection ID %d is already terminated.\n", id);
                LeaveCriticalSection(&peer_list_lock);
                return;
            }
            closesocket(peer_list[i].socket);
            peer_list[i].active = 0;
            printf("Peer %d terminated.\n", id);
            LeaveCriticalSection(&peer_list_lock);
            return;
        }
    }
    printf("Error: Invalid connection ID %d.\n", id);
    LeaveCriticalSection(&peer_list_lock);
}

void send_message_to_peer(int id, const char* msg) {
    if (strlen(msg) > 100) {
        printf("Error: Message exceeds 100 character limit.\n");
        return;
    }
    EnterCriticalSection(&peer_list_lock);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peer_list[i].active && peer_list[i].id == id) {
            send(peer_list[i].socket, msg, strlen(msg), 0);
            printf("Message sent to %d\n", id);
            LeaveCriticalSection(&peer_list_lock);
            return;
        }
    }
    LeaveCriticalSection(&peer_list_lock);
    printf("Error: Invalid connection ID %d.\n", id);
}

void list_peers() {
    printf("id: IP address         Port No.\n");
    EnterCriticalSection(&peer_list_lock);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peer_list[i].active) {
            printf("%d: %s\t%d\n", peer_list[i].id, peer_list[i].ip, peer_list[i].port);
        }
    }
    LeaveCriticalSection(&peer_list_lock);
}

void cleanup_all_peers() {
    EnterCriticalSection(&peer_list_lock);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peer_list[i].active) {
            send(peer_list[i].socket, "EXIT", 4, 0);
            closesocket(peer_list[i].socket);
            peer_list[i].active = 0;
        }
    }
    LeaveCriticalSection(&peer_list_lock);
    WSACleanup();
}

char* get_my_ip() {
    static char ip[INET_ADDRSTRLEN];
    WSADATA wsaData;
    SOCKET temp_sock;
    struct sockaddr_in temp_addr;
    int addrlen = sizeof(temp_addr);

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    temp_sock = socket(AF_INET, SOCK_DGRAM, 0);
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
    struct sockaddr_in addr;
    int len = sizeof(addr);
    getpeername(sock, (struct sockaddr*)&addr, &len);

    while (1) {
        int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) break;
        buffer[bytes_received] = '\0';
        if (strcmp(buffer, "EXIT") == 0) {
            printf("\n[Info] Peer %s:%d has exited.\n> ", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            // Mark peer as inactive
            EnterCriticalSection(&peer_list_lock);
            for (int i = 0; i < MAX_PEERS; i++) {
                if (peer_list[i].socket == sock) {
                    peer_list[i].active = 0;
                    break;
                }
            }
            LeaveCriticalSection(&peer_list_lock);
            break;
        }
        printf("\nMessage received from %s\nSender's Port: %d\nMessage: \"%s\"\n> ",
               inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), buffer);
        fflush(stdout);
    }
    return 0;
}

DWORD WINAPI start_server(LPVOID param) {
    int port = (int)(intptr_t)param;
    WSADATA wsaData;
    SOCKET server_sock, client_sock;
    struct sockaddr_in server, client;
    int c;

    WSAStartup(MAKEWORD(2,2), &wsaData);
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);
    bind(server_sock, (struct sockaddr *)&server, sizeof(server));
    listen(server_sock, 5);
    printf("Chat program started. Listening on port %d.\n> ", port);

    while (1) {
        c = sizeof(struct sockaddr_in);
        client_sock = accept(server_sock, (struct sockaddr *)&client, &c);
        int id = add_peer(client_sock, inet_ntoa(client.sin_addr), ntohs(client.sin_port));
        printf("\n[Info] Connection established with %s:%d (id %d)\n> ", inet_ntoa(client.sin_addr), ntohs(client.sin_port), id);
        CreateThread(NULL, 0, receive_messages, &peer_list[id - 1].socket, 0, NULL);
    }
    return 0;
}

int is_self_connection(const char* ip, int port, int my_port) {
    if (port != my_port) return 0;
    if (strcmp(ip, "127.0.0.1") == 0 || strcmp(ip, "localhost") == 0) return 1;
    return strcmp(ip, get_my_ip()) == 0;
}

int is_duplicate_connection(const char* ip, int port) {
    EnterCriticalSection(&peer_list_lock);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peer_list[i].active && strcmp(peer_list[i].ip, ip) == 0 && peer_list[i].port == port) {
            LeaveCriticalSection(&peer_list_lock);
            return 1;
        }
    }
    LeaveCriticalSection(&peer_list_lock);
    return 0;
}

void connect_to_peer(char* ip, int port, int my_port) {
    if (is_self_connection(ip, port, my_port)) {
        printf("Error: Cannot connect to self.\n");
        return;
    }
    if (is_duplicate_connection(ip, port)) {
        printf("Error: Duplicate connection not allowed.\n");
        return;
    }

    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server;

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

    int id = add_peer(sock, ip, port);
    printf("Connected to %s:%d (id %d)\n", ip, port, id);
    CreateThread(NULL, 0, receive_messages, &peer_list[id - 1].socket, 0, NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    char command[MAX_CMD_LEN];

    InitializeCriticalSection(&peer_list_lock);
    memset(peer_list, 0, sizeof(peer_list));

    CreateThread(NULL, 0, start_server, (LPVOID)(intptr_t)port, 0, NULL);

    while (1) {
        printf("> ");
        fflush(stdout);

        if (!fgets(command, MAX_CMD_LEN, stdin)) break;
        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "help") == 0) {
            printf("help - Show help\nmyip - Show IP\nmyport - Show port\nconnect <ip> <port> - Connect\nlist - List peers\nsend <id> <msg> - Send message\nterminate <id> - Remove peer\nexit - Quit\n");
        } else if (strcmp(command, "myip") == 0) {
            printf("IP Address: %s\n", get_my_ip());
        } else if (strcmp(command, "myport") == 0) {
            printf("Listening on port: %d\n", port);
        } else if (strncmp(command, "connect ", 8) == 0) {
            char ip[INET_ADDRSTRLEN]; int peer_port;
            if (sscanf(command + 8, "%s %d", ip, &peer_port) == 2) {
                connect_to_peer(ip, peer_port, port);
            }
        } else if (strcmp(command, "list") == 0) {
            list_peers();
        } else if (strncmp(command, "send ", 5) == 0) {
            int id; char msg[MAX_MSG_LEN];
            char *msg_start = strchr(command + 5, ' ');
            if (msg_start != NULL) {
                *msg_start = '\0';
                id = atoi(command + 5);
                strncpy(msg, msg_start + 1, MAX_MSG_LEN - 1);
                msg[MAX_MSG_LEN - 1] = '\0';
                send_message_to_peer(id, msg);
            } else {
                printf("Error: Invalid send format. Use: send <id> <message>\n");
            }
        } else if (strncmp(command, "terminate ", 10) == 0) {
            int id;
            if (sscanf(command + 10, "%d", &id) == 1) {
                remove_peer(id);
            } else {
                printf("Error: Invalid connection ID.\n");
            }
        } else if (strcmp(command, "exit") == 0) {
            printf("Shutting down...\n");
            cleanup_all_peers();
            break;
        } else {
            printf("Unknown command. Type 'help' for help.\n");
        }
    }

    DeleteCriticalSection(&peer_list_lock);
    return 0;
}
