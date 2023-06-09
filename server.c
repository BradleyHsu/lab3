#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

typedef struct client_info {
    int socket;                     
    FILE *fragment_file;            
    struct client_info *next;       
} client_info;

typedef struct line_node {
    int line_number;
    char *line;
    struct line_node *next;
} line_node;

// Function declarations
void parse_arguments(int argc, char *argv[], char **input_filename, int *port);
int open_files(char *input_filename, char **output_filename, FILE ***fragment_files, int *num_fragments);
int create_and_bind_socket(int port);
void event_handling(int server_socket, FILE **fragment_files, int num_fragments, char *output_filename);
void manage_data_structures(int client_socket, FILE *fragment_file);
void write_output(const char *output_filename);
void cleanup(int epoll_fd, struct client_info *clients, FILE **fragment_files, int num_fragments);
int accept_client(int server_socket);
void add_client_to_epoll(int epoll_fd, int client_socket);
struct client_info *add_client_to_list(struct client_info *clients, int client_socket, FILE *fragment_file);
struct client_info *find_client(struct client_info *clients, int client_socket);
int handle_client_read(struct client_info *client);
void handle_client_write(struct client_info *client);
void process_client_data(struct client_info *client, const char *data); 
int read_fragment_data(FILE *fragment_file, char *buffer, int buffer_size); 
void insert_line_node(int line_number, const char *line);
line_node *get_head();
void free_line_nodes();
void parse_line(char *input, int *output_int, char **output_str); 

#define MAX_EVENTS 64
#define READ_BUFFER_SIZE 4096
#define WRITE_BUFFER_SIZE 4096


static line_node *head = NULL;

// Main function
int main(int argc, char *argv[]) {
    char *input_filename;
    int port;
    parse_arguments(argc, argv, &input_filename, &port);

    #ifdef DEBUG
        printf("Debug mode enabled\n");
        printf("Input filename: %s\n", input_filename);
        printf("Port: %d\n", port);
    #endif

    char *output_filename;
    FILE **fragment_files;
    int num_fragments;
    open_files(input_filename, &output_filename, &fragment_files, &num_fragments);

    #ifdef DEBUG
        printf("Output filename: %s\n", output_filename);
        printf("Number of fragments: %d\n", num_fragments);
    
        for (int i = 0; i < num_fragments; i++) {
            char buffer[256];
            while(fgets(buffer, sizeof(buffer), fragment_files[i]) != NULL) {
                strtok(buffer, "\n");
                printf("Fragment file %d: %s\n", i, buffer);
            }
            //reset file pointer
            fseek(fragment_files[i], 0, SEEK_SET);
        }
    #endif

    int server_socket = create_and_bind_socket(port);

    #ifdef DEBUG
        printf("Server socket: %d\n", server_socket);
    #endif
    
    event_handling(server_socket, fragment_files, num_fragments, output_filename);

    write_output(output_filename);

    return 0;
}

// Function implementations
void parse_arguments(int argc, char *argv[], char **input_filename, int *port) {
    if (argc != 3) {
        printf("Usage: %s <input_file> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    *input_filename = argv[1];
    *port = atoi(argv[2]);
}

int open_files(char *input_filename, char **output_filename, FILE ***fragment_files, int *num_fragments) {
    FILE *input_file = fopen(input_filename, "r");
    if (!input_file) {
        perror("Error opening input file");
        exit(EXIT_FAILURE);
    }

    char buffer[256];
    if (fgets(buffer, sizeof(buffer), input_file) == NULL) {
        perror("Error reading output filename");
        fclose(input_file);
        exit(EXIT_FAILURE);
    }

    *output_filename = strdup(buffer);

    FILE *output_file = fopen(*output_filename, "w");
    if (!output_file) {
        perror("Error opening output file");
        fclose(input_file);
        exit(EXIT_FAILURE);
    }
    fclose(output_file);

    strtok(*output_filename, "\n");

    // Read fragment file names and open them
    int fragment_count = 0;
    *fragment_files = NULL;
    while (fgets(buffer, sizeof(buffer), input_file) != NULL) {
        fragment_count++;
        *fragment_files = (FILE **)realloc(*fragment_files, sizeof(FILE *) * fragment_count);
        strtok(buffer, "\n");
        printf("Opening fragment file: %s\n", buffer);
        (*fragment_files)[fragment_count - 1] = fopen(buffer, "r");
        if ((*fragment_files)[fragment_count - 1] == NULL) {
            perror("Error opening fragment file");
            // Clean up
            fclose(input_file);
            for (int i = 0; i < fragment_count - 1; i++) {
                fclose((*fragment_files)[i]);
            }
            free(*fragment_files);
            exit(EXIT_FAILURE);
        }
    }

    fclose(input_file);
    *num_fragments = fragment_count;
    return fragment_count;
}

int create_and_bind_socket(int port) {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Error creating server socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding server socket");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    #ifdef DEBUG
        printf("Server socket created and bound\n");
        printf("Server socket: %d\n", server_socket);
    #endif

    listen(server_socket, 5);

    printf("listening at %s:%d\n", inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

    return server_socket;
}

void event_handling(int server_socket, FILE **fragment_files, int num_fragments, char *output_filename) {
    // Set up epoll for event handling
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("Error creating epoll instance");
        exit(EXIT_FAILURE);
    }

    // Add server_socket to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &ev) < 0) {
        perror("Error adding server socket to epoll");
        exit(EXIT_FAILURE);
    }

    // Main event loop
    int completed_clients = 0;
    int connected_clients = 0;
    client_info *clients = NULL;

    while (completed_clients < num_fragments) {

        struct epoll_event events[MAX_EVENTS];
        int ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if(ready < 0) {
            perror("Error waiting for events");
            exit(EXIT_FAILURE);
        }

        #ifdef DEBUG
            for (int i = 0; i < ready; i ++) {
                printf("Event: %d\n", i);
                printf("Event fd: %d\n", events[i].data.fd);
                printf("Event mask: %d\n", events[i].events);
            }
        #endif

        for (int event_idx = 0; event_idx < ready; event_idx++) {
            #ifdef DEBUG
                printf("Event: %d\n", event_idx);
                printf("Event fd: %d\n", events[event_idx].data.fd);
                printf("Event mask: %d\n", events[event_idx].events);
            #endif 
            if (events[event_idx].data.fd == server_socket) {
                // Handle new client connection
                int client_socket = accept_client(server_socket);
                if (client_socket >= 0) {
                    add_client_to_epoll(epoll_fd, client_socket);
                    clients = add_client_to_list(clients, client_socket, fragment_files[connected_clients]);
                    connected_clients++;
                }
            } else {
                // Handle read/write events for clients
                struct client_info *client = find_client(clients, events[event_idx].data.fd);

                if (client == NULL) {
                    perror("Error finding client in list");
                    exit(EXIT_FAILURE);
                }

                if (events[event_idx].events & EPOLLIN) {
                    // Handle read events
                    if (handle_client_read(client)) {
                        // Client completed transfer
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->socket, NULL);
                        close(client->socket);
                        completed_clients++;
                    }
                }

                if (events[event_idx].events & EPOLLOUT) {
                    // Handle write events
                    handle_client_write(client);
                }
            }
        }
    }

    write_output(output_filename);
}

int accept_client(int server_socket) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);

    if (client_socket < 0) {
        #ifdef DEBUG
            printf("Error accepting client connection\n");
            printf("client_socket: %d\n", client_socket);
            printf("client_addr: %s\n", inet_ntoa(client_addr.sin_addr));
        #endif
        return -1;
    }

    return client_socket;
}

void add_client_to_epoll(int epoll_fd, int client_socket) {
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.fd = client_socket;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev) < 0) {
        perror("Error adding client socket to epoll");
        exit(EXIT_FAILURE);
    }
}

struct client_info *add_client_to_list(struct client_info *clients, int client_socket, FILE *fragment_file) {
    struct client_info *new_client = (struct client_info *)malloc(sizeof(struct client_info));
    new_client->socket = client_socket;
    new_client->fragment_file = fragment_file;
    new_client->next = clients;
    return new_client;
}

struct client_info *find_client(struct client_info *clients, int client_socket) {
    struct client_info *current = clients;
    while (current) {
        if (current->socket == client_socket) {
            return current;
        }
        current = current->next;
    }
    perror("Error finding client in list");
    return NULL;
}

int handle_client_read(struct client_info *client) {
    char buffer[READ_BUFFER_SIZE];
    int bytes_read;
    int total_bytes_read = 0;
    int num_loops = 0;

    do {

        bytes_read = read(client->socket, buffer + total_bytes_read, READ_BUFFER_SIZE - 1 - total_bytes_read);

        #ifdef DEBUG
            if (bytes_read > 0) {
                printf("Read from client:\n %s\n", buffer);
            }
        #endif

        if (bytes_read < 0) {
            perror("Error reading from client socket\n");
            return 1;
        }

        total_bytes_read += bytes_read;
        num_loops = num_loops + 1;
        if (num_loops > 10000) {
            printf("Read loop exceeded 10000 iterations\n");
            return 1;
        }
    } while (buffer[total_bytes_read - 1] != '\0');

    if (bytes_read > 0) {
         printf("Finished reading from client\n");
    }

    process_client_data(client, buffer);
    return bytes_read;
}

void handle_client_write(struct client_info *client) {
    char buffer[WRITE_BUFFER_SIZE];
    int bytes_read = read_fragment_data(client->fragment_file, buffer, WRITE_BUFFER_SIZE - 1);
    if (bytes_read <= 0) {
        if (bytes_read < 0) {
            perror("Error reading from fragment file");
        }
        return;
    }

    buffer[bytes_read] = '\0';
    bytes_read += 1;
    int bytes_written;
    int total_bytes_written = 0;

    while (total_bytes_written < bytes_read ) {
        #ifdef DEBUG 
            printf("Writing to client: %s\n", buffer + total_bytes_written);
        #endif
        bytes_written = write(client->socket, buffer + total_bytes_written, bytes_read - total_bytes_written);
        if (bytes_written < 0) {
            perror("Error writing to client socket");
            break;
        }
        total_bytes_written += bytes_written;
    }
}

int read_fragment_data(FILE *fragment_file, char *buffer, int buffer_size) {
    int total_bytes_read = 0;
    int bytes_read;
    
    while ((bytes_read = fread(buffer + total_bytes_read, 1, buffer_size - 1 - total_bytes_read, fragment_file)) > 0) {
        total_bytes_read += bytes_read;
        if (total_bytes_read == buffer_size - 1) {
            break;
        }
    }

    if (ferror(fragment_file)) {
        return -1;
    }

    buffer[total_bytes_read] = '\0';
    return total_bytes_read;
}

void process_client_data(struct client_info *client, const char *data) {
    char *data_copy = strdup(data);
    char *token = strtok(data_copy, "\n");
    
    while (token) {
        int line_number;
        char *line;
        parse_line(token, &line_number, &line);
        #ifdef DEBUG
            printf("Inserting line %d: %s\n", line_number, line);
        #endif
        insert_line_node(line_number, line);
        token = strtok(NULL, "\n");
    }
    
    free(data_copy);
}
void parse_line(char *input, int *output_int, char **output_str) {
    if (!input || !output_int || !output_str) {
        return;
    }
    sscanf(input, "%d", output_int);
    const char *space = strchr(input, ' ');
    if (space) {
        *output_str = strdup(space + 1);
    } else {
        *output_str = strdup("");
    }
}

void write_output(const char *output_file_name) {
    FILE *output_file = fopen(output_file_name, "w");
    if (!output_file) {
        perror("Error opening output file");
        return;
    }

    line_node *current = get_head();
    while (current) {
        fprintf(output_file, "%s\n", current->line);
        current = current->next;
    }

    fclose(output_file);
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

#include <unistd.h> // For close function
#include <stdlib.h> // For free function

void cleanup(int epoll_fd, struct client_info *clients, FILE **fragment_files, int num_fragments) {
    // Close the epoll file descriptor
    close(epoll_fd);

    // Close all fragment files
    for (int i = 0; i < num_fragments; i++) {
        if (fragment_files[i]) {
            fclose(fragment_files[i]);
            fragment_files[i] = NULL;
        }
    }

    // Free the memory allocated for client_info structures
    struct client_info *current_client = clients;
    struct client_info *next_client;
    while (current_client) {
        next_client = current_client->next;
        if (current_client->fragment_file) {
            fclose(current_client->fragment_file);
        }
        close(current_client->socket);
        free(current_client);
        current_client = next_client;
    }
}
