#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include "line_node.h"

typedef struct client_args {
    char *address;
    int port;
} client_args;

client_args parse_arguments(int argc, char *argv[]);
int create_socket_and_connect(const char *address, int port);
char *read_data_from_server(int socket_fd);
void store_data_in_sorted_list(const char *received_data);
void send_sorted_data_to_server(int socket_fd, line_node *head);
void cleanup_and_exit(int socket_fd, line_node *head);

int main(int argc, char *argv[]) {
    client_args args = parse_arguments(argc, argv);
    int socket_fd = create_socket_and_connect(args.address, args.port);
    char *received_data = read_data_from_server(socket_fd);
    store_data_in_sorted_list(received_data);
    line_node *head = get_head();
    send_sorted_data_to_server(socket_fd, head);
    cleanup_and_exit(socket_fd, head);

    return 0;
}

client_args parse_arguments(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <address> <port>\n", argv[0]);
        exit(1);
    }

    client_args args;
    args.address = argv[1];
    args.port = atoi(argv[2]);
    return args;
}

int create_socket_and_connect(const char *address, int port) {
    int socket_fd;
    struct sockaddr_in server_addr;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("Error creating socket");
        exit(2);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, address, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        exit(3);
    }

    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error connecting to server");
        exit(4);
    }

    return socket_fd;
}

// Implement the read_data_from_server, store_data_in_sorted_list, send_sorted_data_to_server, and cleanup_and_exit functions here
