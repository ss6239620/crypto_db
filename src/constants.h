#ifndef MODULE1_H
#define MODULE1_H

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define TABLE_MAX_PAGES 100 // Maximum number of pages allowed for a table
#define INVALID_PAGE_NUM UINT32_MAX

/*
Calculates the size of a specific attribute (field) within a given struct.
Working Steps:
    1) (Struct*)0:
            Imagine a "fake" pointer to the structure Struct. We use 0 (null) as the address, but we don't actually create a real structure. This is just a trick to let the compiler know which structure we're talking about.
    1) ->Attribute:
            Using the "fake" pointer, we access the member (Attribute) of the structure. Again, this is just a trick to tell the compiler which member we're interested in.
    1) sizeof(...):
            IFinally, we use sizeof to calculate the size (in bytes) of that member.
*/
#define size_of_attributes(Struct, Attribute) sizeof(((Struct *)0)->Attribute)

struct InputBuffer_t
{
    // total struct size is 24 byte for 64 bit system
    char *buffer;         // 8 byte for 64-bit architecture
    size_t buffer_length; // 8 byte for 64-bit architecture
    size_t input_length;  // 8 byte for 64-bit architecture
};
typedef struct InputBuffer_t InputBuffer;

struct Row_t
{
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1];
    char email[COLUMN_EMAIL_SIZE + 1];
};
typedef struct Row_t Row;

enum StatementType_t
{
    STATEMENT_INSERT,
    STATEMENT_SELECT,
    STATEMENT_UPDATE,
    STATEMENT_DELETE
};
typedef enum StatementType_t StatementType;

struct Statement_t
{
    StatementType type;
    Row row_to_insert;
};
typedef struct Statement_t Statement;

enum PrepareResult_t
{
    PREPARE_SUCCESS,
    PREPARE_NEGATIVE_ID,
    PREPARE_STRING_TOO_LONG,
    PREPARE_SYNTAX_ERROR,
    PREPARE_UNRECOGNIZED_STATEMENT
};
typedef enum PrepareResult_t PrepareResult;

enum ExecuteResult_t
{
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL,
    EXECUTE_DUPLICATE_KEY,
    EXECUTE_NOT_FOUND
};
typedef enum ExecuteResult_t ExecuteResult;

typedef enum
{
    INTERNAL_NODE,
    LEAF_NODE
} NodeType;

enum MetaCommandResult_t
{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
};
typedef enum MetaCommandResult_t MetaCommandResult;

struct Pager_t
{
    int file_descriptor;          // 4 bytes
    uint32_t file_length;         // 4 bytes
    uint32_t num_pages;           // 4 bytes
    void *pages[TABLE_MAX_PAGES]; // 100 pointers = (void *) 8 bytes * 100 = 800 bytes
};
typedef struct Pager_t Pager;

struct Table_t
{
    Pager *pager;
    uint32_t root_page_num;
};
typedef struct Table_t Table;

typedef struct
{
    Table *table;
    uint32_t page_num;
    uint32_t cell_num;
    bool end_of_table; // Indicates a position one past the last element
} Cursor;

// Constansts For Pager start here
// Offset : offsets are used to determine the starting position of each field (member) within a Row structure when the structure is stored in memory.

const uint32_t ID_SIZE = size_of_attributes(Row, id);
const uint32_t USERNAME_SIZE = size_of_attributes(Row, username);
const uint32_t EMAIL_SIZE = size_of_attributes(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_SIZE + ID_OFFSET;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const uint32_t PAGE_SIZE = 4096;                                 // Maximum size of a single page in bytes
const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;             // Number of rows that can fit in a single page
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES; // Total number of rows that can be stored in the entire table

// Constansts For Pager end here

/*
Each node will correspond to one page. Nodes need to store some metadata in a header at the beginning of the page. Every node will store what type of node it is, whether or not it is the root node, and a pointer to its parent (to allow finding a node’s siblings).

Header Layout start here
*/
// Common Node Header Layout start here
//  The first byte (offset 0) stores the node type (Leaf or Internal)
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;

// The second byte (offset 1) is a flag indicating whether the node is the root
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;

// The next 4 bytes (offset 2-5) store a pointer to the parent node
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;

const uint32_t COMMON_NODE_HEADER_SIZE = PARENT_POINTER_OFFSET + PARENT_POINTER_SIZE;
;
// Common Node Header Layout end here

// Leaft Node Header Layout start here
// The next 4 bytes (offset 6-9) store a number of cells present in current page
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;

/*
To scan the entire table, we need to jump to the second leaf node after we reach the end of the first. To do that, we’re going to save a new field in the leaf node header called “next_leaf”, which will hold the page number of the leaf’s sibling node on the right. The rightmost leaf node will have a next_leaf value of 0 to denote no sibling (page 0 is reserved for the root node of the table anyway).
*/
const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET = LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NUM_CELLS_OFFSET;

const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE;
// Leaft Node Header Layout end here

// Leaf Node Body layout start here (Now the whole page from here is generally used to store cells(Key+value))
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t); // occupies 4 bytes
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
// This occupies the size of the whole row currently(id + username + email = 4 + 33 + 256 = 292 bytes)
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET = LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
// Sum of both key and value to get cell size currently (cell size = key + value = 4 + 292 = 296 bytes)
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
// Available space to store key value pair in page currently (current page size - total header = 4096 - 9 = 4088 bytes)
const uint32_t LEAF_NODE_SPACE_FOR_CELL = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
// Max that can be occupied in one page can be found by dividing Available space by cell size
const uint32_t LEAF_NODE_MAX_CELL = LEAF_NODE_SPACE_FOR_CELL / LEAF_NODE_CELL_SIZE;
const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELL + 1) / 2; // leaf node right split count
// leaf node left split count
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT = (LEAF_NODE_MAX_CELL + 1) - (LEAF_NODE_RIGHT_SPLIT_COUNT);
// Leaf Node Body layout end here

/*
Internal Node constant start here
*/
// Internal Node header layout start here
// The next 4 bytes (offset 6-9) store a number of keys present in current Internal node(aka page).
const uint32_t INTERNAL_NODE_NUM_KEY_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_NUM_KEY_SIZE_OFFSET = COMMON_NODE_HEADER_SIZE;
// The next 4 bytes (offset 10-13) store a Right child present in current Internal node(aka page).
const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET = INTERNAL_NODE_NUM_KEY_SIZE + INTERNAL_NODE_NUM_KEY_SIZE_OFFSET;
// Total Internal Node header size
const uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + INTERNAL_NODE_NUM_KEY_SIZE + INTERNAL_NODE_RIGHT_CHILD_SIZE;
// Internal Node header layout end here

/*
Internal Node Body layout start here
Notice our huge branching factor. Because each child pointer / key pair is so small, we can fit 510 keys and 511 child pointers in each internal node. That means we’ll never have to traverse many layers of the tree to find a given key!
*/
const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CELL_SIZE = INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE;
// Internal Node Body layout end here

/*
INTERNAL_NODE_MAX_CELL = (Page Size − Header Size) / (Poimter size + key size)
*/
const uint32_t INTERNAL_NODE_MAX_CELL = 3;

//db.c
Table *db_open(const char *filename);
void print_prompt();
void db_close(Table *table);

//input.c
InputBuffer *new_input_buffer();
void read_input(InputBuffer *input_buffer);
ssize_t getline(char **lineptr, size_t *n, FILE *stream);

// query_processing.c
MetaCommandResult do_meta_command(InputBuffer *input_buffer, Table *table);
PrepareResult prepare_insert(InputBuffer *input_buffer, Statement *statement);
PrepareResult preare_update(InputBuffer *input_buffer, Statement *statement);
PrepareResult prepare_delete(InputBuffer *input_buffer, Statement *statement);
PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement);
ExecuteResult execute_insert(Statement *statement, Table *table);
ExecuteResult execute_update(Statement *statement, Table *table);
ExecuteResult execute_delete(Statement *statement, Table *table);
ExecuteResult execute_select(Statement *statement, Table *table);
ExecuteResult execute_statement(Statement *statement, Table *table);

// pager.c
Pager *page_open(const char *filename);
void *get_page(Pager *pager, uint32_t page_num);
void pager_flush(Pager *pager, uint32_t page_num);
void serialize_row(Row *source, void *destination);
void deserialize_row(void *source, Row *destination);
uint32_t get_unused_page_num(Pager *pager);

// cursor.c
Cursor *start_table(Table *table);
void *cursor_value(Cursor *cursor);
void cursor_advance(Cursor *cursor);

// internal_node.c
uint32_t *internal_node_num_keys(void *node);
uint32_t *internal_node_right_child(void *node);
uint32_t *internal_node_cell(void *node, uint32_t cell_num);
uint32_t *internal_node_child(void *node, uint32_t child_num);
uint32_t *internal_node_key(void *node, uint32_t key_num);
void initialize_internal_node(void *node);
uint32_t internal_node_find_child(void *node, uint32_t key);
Cursor *find_internal_node(Table *table, uint32_t page_num, uint32_t key);
void internal_node_split_insert(Table *table, uint32_t parent_page_num, uint32_t child_page_num);
void internal_node_insert(Table *table, uint32_t parent_page_num, uint32_t child_page_num);
uint32_t *update_internal_node_key(void *node, uint32_t old_key, uint32_t new_key);

// leaf_node.c
uint32_t *leaf_node_num_cells(void *node);
void *leaf_node_cell(void *node, uint32_t cell_num);
uint32_t *leaf_node_key(void *node, uint32_t cell_num);
uint32_t *leaf_node_next_leaf(void *node);
void *leaf_node_value(void *node, uint32_t cell_num);
void initialize_leaf_node(void *node);
Cursor *find_leaf_node(Table *table, uint32_t page_num, uint32_t key);
void leaf_node_split_insert(Cursor *cursor, uint32_t key, Row *value);
void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value);

// btree.c
NodeType get_node_type(void *node);
void set_node_type(void *node, NodeType type);
bool is_root_node(void *node);
void set_node_root(void *node, bool is_root);
uint32_t *node_parent(void *node);
uint32_t get_node_max_key(Pager *pager, void *node);
Cursor *find_table(Table *table, uint32_t key);
void create_new_root(Table *table, uint32_t right_child_page_num);

// test.c
void print_constant();
void indent(uint32_t level);
void print_row(Row *row);
void print_tree(Pager *pager, uint32_t page_num, uint32_t indentation_level);

#endif