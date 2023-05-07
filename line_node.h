typedef struct line_node {
    int line_number;
    char *line;
    struct line_node *next;
} line_node;

void insert_line_node(int line_number, const char *line);
line_node *get_head();
void free_line_nodes();

