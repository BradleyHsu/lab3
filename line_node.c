#include <stdlib.h>
#include <string.h>
#include "line_node.h"

static line_node *head = NULL;

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
