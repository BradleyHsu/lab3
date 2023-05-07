#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

typedef struct client_args {
    char *address;
    int port;
} client_args;

typedef struct line_node {
    int line_number;
    char *line;
    struct line_node *next;
} line_node;

void insert_line_node(int line_number, const char *line);
line_node *get_head();
void free_line_nodes();
client_args parse_arguments(int argc, char *argv[]);
int create_socket_and_connect(const char *address, int port);
char *read_data_from_server(int socket_fd);
void store_data_in_sorted_list(const char *received_data);
void send_sorted_data_to_server(int socket_fd, line_node *head);
void cleanup_and_exit(int socket_fd, line_node *head);

static line_node *head = NULL;

int main(int argc, char *argv[]) {
    client_args args = parse_arguments(argc, argv);
    printf("Connecting to %s:%d\n", args.address, args.port);
    int socket_fd = create_socket_and_connect(args.address, args.port);
    printf("Connected\n");
    char *received_data = read_data_from_server(socket_fd);
    store_data_in_sorted_list(received_data);
    line_node *head = get_head();
    send_sorted_data_to_server(socket_fd, head);
    cleanup_and_exit(socket_fd, head);

    return 0;
}

void insert_line_node(int line_number, const char *line) {
    line_node *new_node = (line_node *)malloc(sizeof(line_node));
    new_node->line_number = line_number;
    new_node->line = strdup(line);
    new_node->next = NULL;

    if (!head || line_number < head->line_number) {
        new_node->next = head;
        head = new_node;
    } else {
        line_node *current = head;
        while (current->next && current->next->line_number < line_number) {
            current = current->next;
        }
        new_node->next = current->next;
        current->next = new_node;
    }
}

line_node *get_head() {
    return head;
}

void free_line_nodes() {
    line_node *current = head;
    while (current) {
        line_node *next = current->next;
        free(current->line);
        free(current);
        current = next;
    }
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
char *read_data_from_server(int socket_fd) {
    const int buffer_size = 1024;
    char *data = NULL;
    size_t data_size = 0;
    ssize_t bytes_read;
    char buffer[buffer_size];

    while ((bytes_read = read(socket_fd, buffer, buffer_size - 1)) > 0) {
        buffer[bytes_read] = '\0';
        data = realloc(data, data_size + bytes_read + 1);
        strncpy(data + data_size, buffer, bytes_read + 1);
        data_size += bytes_read;
    }

    if (bytes_read < 0) {
        perror("Error reading from server");
        exit(5);
    }

    return data;
}

void store_data_in_sorted_list(const char *received_data) {
    const char *delimiter = "\n";
    char *data = strdup(received_data);
    char *line = strtok(data, delimiter);
    int line_number;

    while (line) {
        sscanf(line, "%d", &line_number);
        char *text_start = strchr(line, ' ') + 1;
        insert_line_node(line_number, text_start);
        line = strtok(NULL, delimiter);
    }

    free(data);
}

void send_sorted_data_to_server(int socket_fd, line_node *head) {
    char buffer[1024];
    line_node *current = head;

    while (current) {
        int bytes_written = snprintf(buffer, 1024, "%d %s\n", current->line_number, current->line);
        if (write(socket_fd, buffer, bytes_written) != bytes_written) {
            perror("Error writing to server");
            exit(6);
        }
        current = current->next;
    }
}

void cleanup_and_exit(int socket_fd, line_node *head) {
    close(socket_fd);
    free_line_nodes(head);
}
