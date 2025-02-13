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
    STATEMENT_SELECT
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
    EXECUTE_TABLE_FULL
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

const uint32_t COMMON_NODE_HEADER_SIZE = NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_OFFSET;
// Common Node Header Layout end here

// Leaft Node Header Layout start here
// The next 4 bytes (offset 6-9) store a number of cells present in current page
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;

const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;
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
// Leaf Node Body layout end here

/**
 * @brief Returns a pointer to the number of cells (key-value pairs) in a leaf node.
 *
 * Each leaf node in the B-tree has a header containing metadata, including the
 * number of stored cells. This function provides a pointer to that value,
 * allowing direct access and modification.
 *
 * @param node A pointer to the start of the leaf node in memory.
 *
 * @return A pointer to the number of cells stored in the leaf node.
 */
uint32_t *leaf_node_num_cells(void *node)
{
    return node + LEAF_NODE_NUM_CELLS_OFFSET;
}

/**
 * @brief Retrieves a pointer to a specific cell (key-value pair) in a leaf node (aka page).
 *
 * A leaf node consists of a header (metadata) and a body that stores key-value pairs sequentially.
 * This function calculates the memory offset for the specified cell and returns a pointer to it.
 *
 * The offset is determined by skipping the header and adding an offset based on the cell index.
 *
 * @param node A pointer to the beginning of the leaf node (aka page) in memory.
 * @param cell_num The index of the cell to retrieve (0-based index).
 *
 * @return A pointer to the memory location of the requested cell within the leaf node.
 */
void *leaf_node_cell(void *node, uint32_t cell_num)
{
    return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

/**
 * @brief Retrieves the key of a specific cell in a leaf node (aka page).
 *
 * Each cell in a leaf node contains a key-value pair. This function locates the requested cell
 * and extracts the key from it.
 *
 * @param node A pointer to the beginning of the leaf node (aka page) in memory.
 * @param cell_num The index of the cell to retrieve the key from (0-based index).
 *
 * @return The key (an unsigned 32-bit integer) stored in the specified cell.
 */
uint32_t *leaf_node_key(void *node, uint32_t cell_num)
{
    return leaf_node_cell(node, cell_num);
}

/**
 * @brief Retrieves a pointer to the value stored in a specific cell of a leaf node (aka page).
 *
 * Each cell in a leaf node consists of a key-value pair. The key is stored at the beginning,
 * followed by the value. This function calculates the offset to access the value by skipping
 * the key portion of the cell.
 *
 * @param node A pointer to the beginning of the leaf node (aka page) in memory.
 * @param cell_num The index of the cell to retrieve the value from (0-based index).
 *
 * @return A pointer to the memory location where the value is stored in the specified cell.
 */
void *leaf_node_value(void *node, uint32_t cell_num)
{
    return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

/**
 * @brief Initializes a leaf node (aka page) by setting the number of cells to zero.
 *
 * This function prepares a new leaf node by ensuring it starts with zero key-value pairs.
 * The number of cells in a leaf node is stored in the header at a fixed offset.
 *
 * @param node A pointer to the beginning of the leaf node (aka page) in memory.
 */
void initialize_leaf_node(void *node) { *leaf_node_num_cells(node) = 0; }

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

/*
This function is responsible for retrieving a page from the pager. It handles both retrieving an already cached page and loading a page from a file into memory if it is not yet cached.
    Parameters:
        Pager *pager → A pointer to the Pager structure that manages database pages.
        uint32_t page_num → The page number being requested.
    return value:
        void * → The function returns a pointer to the requested page.

    Working step:
        1) Check if page_num is Valid
        2) Check if Page is Already Loaded in Memory
        3) Allocate Memory for the Page
        4) Determine the Number of Pages in the File
        5) Load the Page from the File (if it exists)
        6) Store the Page in Memory
 */
void *get_page(Pager *pager, uint32_t page_num)
{
    if (page_num > TABLE_MAX_PAGES)
    {
        printf("Tride to fetch page total page. %d > %d", page_num, TABLE_MAX_PAGES);
        exit(EXIT_FAILURE);
    }

    // Cache miss. Allocate memory and load from file.
    if (pager->pages[page_num] == NULL)
    {
        void *page = malloc(PAGE_SIZE); // Alllocate a new page of PAGE_SIZE memory

        uint32_t num_pages = pager->file_length / PAGE_SIZE; // Determine the Number of Pages in the File
        /**
         The function calculates how many pages are currently stored in the database file.
         If the file size is not a perfect multiple of PAGE_SIZE, then there is a partial page at the end.
         In this case, we increment num_pages to include that partial page.
         */
        if (pager->file_length % PAGE_SIZE)
            num_pages += 1;

        if (page_num <= num_pages)
        {
            lseek(pager->file_descriptor, PAGE_SIZE * page_num, SEEK_SET);
            // page size chunks of data is read from file and than stored on page variable described above
            ssize_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);
            if (bytes_read == -1)
            {
                printf("Error reading file: %d\n", errno);
                exit(EXIT_FAILURE);
            }
        }
        pager->pages[page_num] = page;

        if (page_num >= pager->num_pages)
        {
            pager->num_pages = page_num + 1;
        }
    }
    return pager->pages[page_num];
}

/**
 * @brief Initializes a Cursor that points to the first row of a table.
 *
 * Functionality:
        - Allocates memory for a Cursor.
        - Sets the cursor to start at the root page of the table.
        - Initializes the cell position to 0 (beginning of the page).
        - Retrieves the root node and checks the number of cells in it.
        - If the table has no rows, marks the cursor as `end_of_table`.
 *
 * @param table A pointer to the table from which the cursor will start.
 *
 * @return `cursor` A pointer to a Cursor representing the starting position in the table.
 */
Cursor *start_table(Table *table)
{
    Cursor *cursor = malloc(sizeof(Cursor)); // Allocate memory
    cursor->table = table;                   // The cursor stores a reference to the table.
    cursor->page_num = table->root_page_num; // Start at the root page of the table
    cursor->cell_num = 0;                    // Start at the first cell (row) in the root page

    // Get the root page (which is a leaf node in this case)
    void *root_node = get_page(table->pager, table->root_page_num);
    uint32_t num_cell = *leaf_node_num_cells(root_node); // Get the number of cells (rows) in the root node
    // If there are no rows, mark the cursor as being at the end of the table
    cursor->end_of_table = (num_cell == 0);
    return cursor;
}

/**
 * @brief  Creates and initializes a Cursor that points to the end of a table.
    This is useful for inserting new rows at the end.
 *
 * Functionality:
        - Allocates memory for a Cursor.
        - Sets the cursor to point to the root page of the table.
        - Retrieves the root node and determines the number of existing cells (rows).
        - Positions the cursor at `cell_num = num_cells`, meaning it points to the next available row.
        - Marks `end_of_table = true` to indicate it is at the end.
 *
 * @param table A pointer to the table from which the cursor will start.
 *
 * @return `cursor` A pointer to a Cursor representing the starting position in the table.
 */
Cursor *end_table(Table *table)
{
    Cursor *cursor = malloc(sizeof(Cursor)); // Allocate memory
    cursor->table = table;                   // The cursor stores a reference to the table.
    cursor->page_num = table->root_page_num; // Set the cursor to the root page of the table.

    // Retrieve the root page (assumed to be a leaf node)
    void *root_node = get_page(table->pager, table->root_page_num);
    uint32_t num_cell = *leaf_node_num_cells(root_node); // Get the number of cells (rows) in the root node
    cursor->cell_num = num_cell;                         // Position the cursor at the end
    cursor->end_of_table = true;                         // Indicate that the cursor is at the end of the table
    return cursor;
}

/**
 * @brief Advances the cursor to the next row in the table.
 *
 * This function moves the cursor to the next cell in the current leaf node.
 * If the cursor reaches the end of the node, it sets the `end_of_table` flag to true.
 *
 * @param cursor A pointer to a Cursor object, which tracks the current position in the table.
 */
void cursor_advance(Cursor *cursor)
{
    void *node = get_page(cursor->table->pager, cursor->page_num); // Fetch the current node (page) from the pager

    cursor->cell_num += 1; // Move the cursor to the next cell
    // If the cursor moves past the last cell in the node, mark the end of the table
    if (cursor->cell_num >= (*leaf_node_num_cells(node)))
        cursor->end_of_table = true;
}

// This function print select result.
void print_row(Row *row)
{
    printf("%d, %s, %s\n", row->id, row->username, row->email);
}

/*
The serialize_row function is responsible for converting (serializing) a Row structure into a binary format so that it can be stored in a buffer (destination).
    Parameters:
        Row *source → A pointer to the Row structure that contains the data to be serialized.
        void *destination → A pointer to a block of memory (buffer) where the serialized data will be
    return value: The function does not return a value.
*/
void serialize_row(Row *source, void *destination)
{
    // Copies the id field from source to destination at the correct offset (ID_OFFSET).
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE); // same for username
    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);          // same for email
}

/*
The deserialize_row function reconstructs a Row structure from a serialized binary format stored in a memory buffer (source). Essentially, this function reverses what serialize_row did.
    Parameters:
        void *source → A pointer to a block of memory containing serialized Row data.
        Row *destination → A pointer to a Row structure where the deserialized data will be stored.
    return value: The function does not return a value.
*/
void deserialize_row(void *source, Row *destination)
{
    // Copies the id field from source into destination->id
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE); // same here
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);          // same here
}

/**
 * @brief Retrieves the memory location of a specific row in a table.
 *
 * This function uses paging to locate the correct memory page and then determines
 * the exact byte offset of the row within that page.
 *
 * @param cursor A pointer to a Cursor object, which tracks the current position in the table.
 *               It contains details like the table reference, pager, page number, and cell number.
 *
 * @return A pointer to the location in memory where the specified row is stored.
 */
void *cursor_value(Cursor *cursor)
{
    void *page = get_page(cursor->table->pager, cursor->page_num); // Fetch the Page from the Pager
    return leaf_node_value(page, cursor->cell_num);
}

/*
Page_open perform following functionality:
    Opens (or creates) a file.
    Gets its size.
    Allocates memory for a Pager struct.
    Initializes the struct and returns a pointer to it.
*/
Pager *page_open(const char *filename)
{
    int fd = open(filename,
                  O_RDWR |     // Open for reading and writing
                      O_CREAT, // Create file if it does not exist
                  S_IWUSR |    // User write permission
                      S_IRUSR  // User read permission
    );
    if (fd == -1)
    {
        printf("Unable to open the file \n");
        exit(EXIT_FAILURE);
    }
    // Getting File Size off_t is usually a 64-bit integer
    off_t file_length = lseek(fd, 0, SEEK_END); // Moves the file offset to the end (SEEK_END). Returns the file size

    // Allocating Memory for Pager
    Pager *pager = (Pager *)malloc(sizeof(Pager));
    pager->file_descriptor = fd;
    pager->file_length = file_length;
    pager->num_pages = (file_length / PAGE_SIZE);

    if (file_length % PAGE_SIZE)
    {
        printf("Db file does not have whole number pages it is likely corrupted\n");
        exit(EXIT_FAILURE);
    }

    //  Initializing Page Pointers as NULL
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++)
        pager->pages[i] = NULL;

    return pager;
}

/**
 * @brief Opens a database file and initializes the table structure.
 *
 * This function loads the database file and sets up the pager for managing pages.
 * If the database file is new (empty), it initializes the first page as a leaf node.
 *
 * @param filename The name of the database file to open.
 * @return A pointer to a Table structure representing the database.
 */
Table *db_open(const char *filename)
{
    Pager *pager = page_open(filename);

    Table *table = (Table *)malloc(sizeof(Table));
    table->pager = pager;
    table->root_page_num = 0;

    if (pager->num_pages == 0)
    {
        // New database file. Initialize page 0 as leaf node.
        void *root = get_page(pager, 0);
        initialize_leaf_node(root);
    }

    return table;
}

void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value)
{
    void *node = get_page(cursor->table->pager, cursor->page_num);

    uint32_t num_cell = *leaf_node_num_cells(node);
    if (num_cell >= LEAF_NODE_MAX_CELL)
    {
        // Node (page) full need branching.
        printf("Needs to implement branching.\n");
        exit(EXIT_FAILURE);
    }
    if (cursor->cell_num < num_cell)
    {
        // Make room for new cell
        for (uint32_t i = num_cell; i > cursor->cell_num; i--)
        {
            memcpy(leaf_node_cell(node, i), leaf_node_cell(node, i - 1), LEAF_NODE_CELL_SIZE);
        }
    }
    *(leaf_node_num_cells(node)) += 1;
    *(leaf_node_key)(node, cursor->cell_num) = key;
    serialize_row(value, leaf_node_value(node, cursor->cell_num));
}

// Intialize new input buffer
InputBuffer *new_input_buffer()
{
    InputBuffer *input_buffer = (InputBuffer *)malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
    size_t pos = 0;
    int c;

    if (*lineptr == NULL || *n == 0)
    {
        *n = 128; // Initial buffer size
        *lineptr = malloc(*n);
        if (*lineptr == NULL)
        {
            return -1; // Allocation failed
        }
    }

    while ((c = fgetc(stream)) != EOF)
    {
        // Resize the buffer if necessary
        if (pos + 1 >= *n)
        {
            size_t new_size = *n + (*n >> 2); // Increase by 25%
            char *new_ptr = realloc(*lineptr, new_size);
            if (new_ptr == NULL)
            {
                free(*lineptr); // Free the old buffer before returning
                return -1;      // Reallocation failed
            }
            *lineptr = new_ptr;
            *n = new_size;
        }

        (*lineptr)[pos++] = (char)c;
        if (c == '\n')
        {
            break;
        }
    }

    // Null-terminate the string
    (*lineptr)[pos] = '\0';

    // Return the number of bytes read (excluding the null terminator)
    return pos == 0 && c == EOF ? -1 : (ssize_t)pos;
}

/*
The function does not automatically handle multiple lines unless called repeatedly in a loop.
*/
void read_input(InputBuffer *input_buffer)
{
    /*
    here we will use getline to fetch input getline(&buffer,&size,stdin);
    Parameters:
        &buffer is the address of the first character position where the input string will be stored.
        &size is the address of the variable that holds the size of the input buffer, another pointer.
        stdin is the input file handle. So you could use getline() to read a line of text from a file, but when stdin is specified, standard input is read.
    Working:
        getline() dynamically resizes buffer to fit the input.
        It stores the line of input (including the newline \n) into input_buffer->buffer.
        It updates input_buffer->buffer_length to reflect the allocated buffer size.
        It returns the number of bytes read (bytes_read), including the newline character (\n).
    Example:
        Input: hello world
        getline() will store "hello world\n" in input_buffer->buffer.
        bytes_read will be 12 (11 characters + \n).
    */
    ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

    if (bytes_read <= 0)
    {
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    }

    // Check if the input ends with a newline
    if (input_buffer->buffer[bytes_read - 1] == '\n')
    {
        input_buffer->buffer[bytes_read - 1] = '\0'; // Replace newline with null terminator
        input_buffer->input_length = bytes_read - 1;
    }
    else
    {
        input_buffer->input_length = bytes_read; // No newline, use the full length
    }
}

/**
 * @brief Writes a specific page from memory to the database file on disk.
 *
 * This function ensures that any modifications made to a page in memory (pager->pages[])
 * are saved to the database file, preventing data loss before program termination
 * or continued execution.
 *
 * @param pager A pointer to the Pager struct that manages the database file.
 * @param page_num The index of the page to be flushed (written) to disk.
 */
void pager_flush(Pager *pager, uint32_t page_num)
{
    if (pager->pages[page_num] == NULL)
    {
        printf("Tried to flush null page\n");
        exit(EXIT_FAILURE);
    }
    off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET); // SEEK_SET offset is calculated from begining.

    if (offset == -1)
    {
        printf("Error seeking: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    // If we flush the file we write the data inside pager to databse so it isnt lost.
    ssize_t byte_written = write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);

    if (byte_written == -1)
    {
        printf("Error Saving: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

/*
The function db_close is responsible for closing the database properly. It does the following:
    1) Writes all pages in memory to the database file.
    2) Frees allocated memory for pages.
    3) Closes the file descriptor (database file).
    4) Frees the Pager struct.
*/
void db_close(Table *table)
{
    Pager *pager = table->pager; // Pointer to the table

    for (uint32_t i = 0; i < pager->num_pages; i++)
    {
        if (pager->pages[i] == NULL)
            continue;
        pager_flush(pager, i); // save to db
        free(pager->pages[i]);
        pager->pages[i] = NULL;
    }

    int result = close(pager->file_descriptor); // close the file descriptor
    if (result == -1)
    {
        printf("Error closing database: \n");
        exit(EXIT_FAILURE);
    }
    // Again check the full page if anything is there instead of null in those pointer remove it
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++)
    {
        void *page = pager->pages[i];
        if (page)
        {
            free(page);
            pager->pages[i] = NULL;
        }
    }
    // Finally free the newly created paer too.
    free(pager);
}

void print_prompt()
{
    printf("crypto> ");
}

void print_constant()
{
    printf("ROW_SIZE: %d\n", ROW_SIZE);
    printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
    printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
    printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
    printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELL);
    printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELL);
}

void print_leaf_node(void *node)
{
    uint32_t num_cells = *leaf_node_num_cells(node);
    for (uint32_t i = 0; i < num_cells; i++)
    {
        uint32_t key = *leaf_node_key(node, i);
        printf(" - %d : %d", i, key);
    }
}

MetaCommandResult do_meta_command(InputBuffer *input_buffer, Table *table)
{
    if (strcmp(input_buffer->buffer, ".exit") == 0)
    {
        db_close(table);
        exit(EXECUTE_SUCCESS);
    }
    else if (strcmp(input_buffer->buffer, ".constant") == 0)
    {
        printf("Constants:\n");
        print_constant();
        return META_COMMAND_SUCCESS;
    }
    else if (strcmp(input_buffer->buffer, ".btree") == 0)
    {
        printf("Tree:\n");
        print_leaf_node(get_page(table->pager, 0));
        return META_COMMAND_SUCCESS;
    }
    else
    {
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}

/*
The prepare_insert function parses user input, extracts ID, username, and email, validates them, and stores the values in a Statement structure.
    Parameters:
        InputBuffer *input_buffer-> Contains the raw user input as a string.
        Statement *statement-> A structure where the parsed statement type and row data will be stored.
    return value:
        Returns an enum value (PrepareResult) indicating success or the type of error encountered.
 */
PrepareResult prepare_insert(InputBuffer *input_buffer, Statement *statement)
{
    // set statement->type to insert
    statement->type = STATEMENT_INSERT;

    // The strtok() function splits a string into multiple pieces (referred to as "tokens") using delimiters.
    // On the first call, strtok() takes the input string and returns the first token.
    // On subsequent calls, strtok(NULL, delim) continues splitting from where it left off, returning the next token.
    char *keyword = strtok(input_buffer->buffer, " "); // store insert
    char *id_string = strtok(NULL, " ");               // store id
    char *username = strtok(NULL, " ");                // store username
    char *email = strtok(NULL, " ");                   // store email

    if (id_string == NULL || username == NULL || email == NULL)
    {
        return PREPARE_SYNTAX_ERROR;
    }
    int id = atoi(id_string);
    if (id < 0)
        return PREPARE_NEGATIVE_ID;

    if (strlen(username) > COLUMN_USERNAME_SIZE)
        return PREPARE_STRING_TOO_LONG;
    if (strlen(email) > COLUMN_EMAIL_SIZE)
        return PREPARE_STRING_TOO_LONG;

    statement->row_to_insert.id = id;
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);

    return PREPARE_SUCCESS;
}

PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement)
{
    if (strncmp(input_buffer->buffer, "insert", 6) == 0)
    {
        return prepare_insert(input_buffer, statement);
    }
    if (strcmp(input_buffer->buffer, "select") == 0)
    {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

ExecuteResult execute_insert(Statement *statement, Table *table)
{
    void *node = get_page(table->pager, table->root_page_num);
    if ((*leaf_node_num_cells(node) >= LEAF_NODE_MAX_CELL))
        return EXECUTE_TABLE_FULL;

    Row *row_to_insert = &(statement->row_to_insert);
    Cursor *cursor = end_table(table);

    leaf_node_insert(cursor, row_to_insert->id, row_to_insert);

    free(cursor);
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement *statement, Table *table)
{
    Cursor *cursor = start_table(table);
    Row row; // if we use Row *row than memory is unintialized and program may crash to avoid this using Row row

    while (!(cursor->end_of_table))
    {
        deserialize_row(cursor_value(cursor), &row);
        print_row(&row);
        cursor_advance(cursor);
    }
    free(cursor);
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement *statement, Table *table)
{
    switch (statement->type)
    {
    case (STATEMENT_INSERT):
        return execute_insert(statement, table);
    case (STATEMENT_SELECT):
        return execute_select(statement, table);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Must supply a database filename.\n");
        exit(EXIT_FAILURE);
    }

    char *filename = argv[1];
    Table *table = db_open(filename);

    InputBuffer *input_buffer = new_input_buffer();
    while (true)
    {
        print_prompt();
        read_input(input_buffer);

        if (input_buffer->buffer[0] == '.')
        {
            switch (do_meta_command(input_buffer, table))
            {
            case (META_COMMAND_SUCCESS):
                continue;
            case (META_COMMAND_UNRECOGNIZED_COMMAND):
                printf("Unrecognized command %s\n", input_buffer->buffer);
                continue;
            }
        }

        Statement statement;
        switch (prepare_statement(input_buffer, &statement))
        {
        case (PREPARE_SUCCESS):
            break;
        case (PREPARE_NEGATIVE_ID):
            printf("Id must be positive.\n");
            continue;
        case (PREPARE_STRING_TOO_LONG):
            printf("String given is too long.\n");
            continue;
        case (PREPARE_SYNTAX_ERROR):
            printf("Syntax error could not parse statement.\n");
            continue;
        case (PREPARE_UNRECOGNIZED_STATEMENT):
            printf("Unrecognized statement found.\n");
            continue;
        }
        switch (execute_statement(&statement, table))
        {
        case (EXECUTE_SUCCESS):
            printf("Statement executed.\n");
            break;
        case (EXECUTE_TABLE_FULL):
            printf("Error Table full.\n");
            break;
        }
    }

    return 0;
}