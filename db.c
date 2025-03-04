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

/**
 * is_root_node - Checks if the given node is the root of the B-tree.
 *
 * @node: Pointer to the node in memory.
 *
 * Return: `true` if the node is a root node, otherwise `false`.
 *
 * Description:
 * - Reads the `IS_ROOT_OFFSET` location in memory, which stores whether
 *   the node is a root (1) or not (0).
 * - Converts the stored `uint8_t` value to a boolean (`true` or `false`).
 */
bool is_root_node(void *node)
{
    uint8_t value = *((uint8_t *)(node + IS_ROOT_OFFSET));
    return (bool)value;
}

/**
 * set_node_root - Sets or clears the root status of a given node.
 *
 * @node: Pointer to the node in memory.
 * @is_root: Boolean value indicating whether the node should be a root (`true`) or not (`false`).
 *
 * Return: None.
 *
 * Description:
 * - Writes the `is_root` flag (converted to `uint8_t`) to `IS_ROOT_OFFSET`.
 * - A value of `1` means the node is a root, and `0` means it is not.
 * - Used when creating a new root or changing the tree structure.
 */
void set_node_root(void *node, bool is_root)
{
    uint8_t value = is_root;
    *((uint8_t *)(node + IS_ROOT_OFFSET)) = value;
}

/**
 * @brief Retrieves the parent pointer of a given B-tree node.
 *
 * Functionality:
 *      - Given a pointer to a node in the B-tree, this function returns a pointer
 *        to the memory location where the **parent node's page number** is stored.
 *      - This is used to traverse **up the tree**, allowing operations like
 *        splitting and merging nodes to update parent references.
 *
 * @param node A pointer to the current node in memory.
 *
 * @return `uint32_t*` A pointer to the memory location storing the parent node's page number.
 *
 * Example Usage:
 * ```
 * uint32_t parent_page = *node_parent(current_node);
 * if (parent_page != 0) {
 *     void *parent = get_page(pager, parent_page);
 *     // Perform operations on the parent node
 * }
 * ```
 */
uint32_t *node_parent(void *node)
{
    return node + PARENT_POINTER_OFFSET;
}

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
 * @brief  Retrieves the pointer to the next leaf node in a B-tree.
 *
 * Functionality:
 *      - A **leaf node** in a B-tree may have a pointer to the **next leaf node** for efficient traversal.
 *      - This function returns a pointer to that **next leaf node offset**.
 *      - Used in **sequential scans** where nodes are linked for faster ordered access.
 *
 * @param node A pointer to the current leaf node in the B-tree.
 *
 * @return `uint32_t*` A pointer to the memory location storing the next leaf node's page number.
 *
 * Example Usage:
 * ```
 * uint32_t next_leaf = *leaf_node_next_leaf(current_node);
 * if (next_leaf != 0) {
 *     void *next_node = get_page(pager, next_leaf);
 * }
 * ```
 */
uint32_t *leaf_node_next_leaf(void *node)
{
    return node + LEAF_NODE_NEXT_LEAF_OFFSET;
}

/**
 * @brief Retrieves the node type from a given node pointer.
 *
 * This function takes a pointer to a node and extracts its type by
 * reading the value at the specified offset (`NODE_TYPE_OFFSET`).
 * The extracted value is then cast to the `NodeType` enumeration.
 *
 * @param node Pointer to the node whose type is to be determined.
 * @param node_type Unused parameter, likely intended for future use
 *                  or mistakenly included.
 * @return NodeType The extracted node type.
 */
NodeType get_node_type(void *node)
{
    uint8_t value = *((uint8_t *)node + NODE_TYPE_OFFSET); // this fetch 0-1 byte from metadata in leaf node(aka page).
    return (NodeType)value;                                // Cast the extracted value to the NodeType enumeration and return it
}

/**
 * @brief Sets the node type for a given node pointer.
 *
 * This function assigns the provided node type to the specified memory
 * location within the node structure. It writes the `node_type` value
 * at the offset `NODE_TYPE_OFFSET` from the node's base address.
 *
 * @param node Pointer to the node whose type is to be set.
 * @param node_type The node type to be assigned.
 */
void set_node_type(void *node, NodeType node_type)
{
    uint8_t value = node_type;                     // cast node_type which is 4 byte to 1 byte.
    *((uint8_t *)node + NODE_TYPE_OFFSET) = value; // this set 1 byte node type to metadata in leaf node(aka page).
}

/**
 * @brief Initializes a leaf node (aka page) by setting the number of cells to zero.
 *
 * This function prepares a new leaf node by ensuring it starts with zero key-value pairs.
 * The number of cells in a leaf node is stored in the header at a fixed offset.
 *
 * @param node A pointer to the beginning of the leaf node (aka page) in memory.
 */
void initialize_leaf_node(void *node)
{
    set_node_type(node, LEAF_NODE);
    set_node_root(node, false);
    *leaf_node_num_cells(node) = 0;
    *leaf_node_next_leaf(node) = 0;
    *node_parent(node) = 0;
}

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

/*
Methods for reading and writing to an internal node:
*/

/**
 * Retrieves a pointer to the number of keys stored in an internal node.
 *
 * @param node Pointer to the internal node in memory.
 * @return Pointer to the memory location where the number of keys is stored.
 *
 * The number of keys in an internal node is stored at a fixed offset
 * (`INTERNAL_NODE_NUM_KEY_SIZE_OFFSET`) after the common node header.
 * This function helps in accessing and modifying the key count directly.
 */
uint32_t *internal_node_num_keys(void *node)
{
    return node + INTERNAL_NODE_NUM_KEY_SIZE_OFFSET;
}

/**
 * Retrieves a pointer to the rightmost child pointer in an internal node.
 *
 * @param node Pointer to the internal node in memory.
 * @return Pointer to the memory location where the rightmost child is stored.
 *
 * In an internal node, the rightmost child pointer is stored at a fixed offset
 * (`INTERNAL_NODE_RIGHT_CHILD_OFFSET`) after the common node header and key count.
 * This function allows access to that pointer for reading or updating.
 */
uint32_t *internal_node_right_child(void *node)
{
    return node + INTERNAL_NODE_RIGHT_CHILD_OFFSET;
}

/**
 * Retrieves a pointer to a specific key-child pair (cell) in an internal node.
 *
 * @param node Pointer to the internal node in memory.
 * @param cell_num The index of the key-child pair within the internal node.
 * @return Pointer to the memory location where the specified cell is stored.
 *
 * Internal nodes store child pointers and keys in pairs, starting after the node header.
 * Each cell consists of a child pointer followed by a key. This function calculates the
 * correct memory location based on the given cell index.
 */
uint32_t *internal_node_cell(void *node, uint32_t cell_num)
{
    return node + INTERNAL_NODE_HEADER_SIZE + cell_num * INTERNAL_NODE_CELL_SIZE;
}

/**
 * Retrieves a pointer to a specific child pointer in an internal node.
 *
 * @param node Pointer to the internal node in memory.
 * @param child_num The index of the child pointer to retrieve.
 * @return Pointer to the memory location where the specified child pointer is stored.
 *
 * Internal nodes store child pointers in two locations:
 * - The first `num_key` children are stored within cells (`internal_node_cell`).
 * - The rightmost child (which has no associated key) is stored separately
 *   at `internal_node_right_child`.
 *
 * This function ensures that `child_num` is within a valid range before accessing it.
 * If `child_num` is equal to `num_key`, it returns the rightmost child pointer.
 * Otherwise, it returns the pointer to the corresponding child-key pair.
 */
uint32_t *internal_node_child(void *node, uint32_t child_num)
{
    uint32_t num_key = *internal_node_num_keys(node);
    if (child_num > num_key)
    {
        printf("Tried to acess child_num %d > num_key %d", child_num, num_key);
        exit(EXIT_FAILURE);
    }
    // if child_num is equal to number of keys return the right most child
    else if (child_num == num_key)
    {
        uint32_t *right_child = internal_node_right_child(node);
        if (*right_child == INVALID_PAGE_NUM)
        {
            printf("Tried t access right child of node, but was invalid page. \n");
            exit(EXIT_FAILURE);
        }
        return right_child;
    }
    else
    {
        uint32_t *child = internal_node_cell(node, child_num);
        if (*child == INVALID_PAGE_NUM)
        {
            printf("Tried to access child %d of node, but was invalid page\n", child_num);
            exit(EXIT_FAILURE);
        }
        return child;
    }
}

/**
 * internal_node_key - Retrieves a pointer to the key at a given index in an internal node.
 *
 * @node: Pointer to the internal node (page) in memory.
 * @key_num: Index of the key to retrieve.
 *
 * Return: A pointer to the key at the specified index.
 *
 * Description:
 * - Internal nodes store key-value pairs, where each key is associated with a child pointer.
 * - Each key is stored after its corresponding child pointer within the node.
 * - This function calculates the address of the key by getting the address of the cell
 *   at `key_num` using `internal_node_cell()` and then adding `INTERNAL_NODE_CHILD_SIZE`
 *   to skip the child pointer.
 *
 * Example:
 * Given an internal node storing keys `[10, 20, 30]` with corresponding children,
 * calling `internal_node_key(node, 1)` will return the address of key `20`.
 */
uint32_t *internal_node_key(void *node, uint32_t key_num)
{
    return internal_node_cell(node, key_num) + INTERNAL_NODE_CHILD_SIZE;
}

/**
 * initialize_internal_node - Initializes a new internal node in the B-tree.
 *
 * @node: Pointer to the memory location of the node.
 *
 * Return: None.
 *
 * Description:
 * - Sets the node type to `INTERNAL_NODE`, meaning it will store keys and child pointers.
 * - Marks the node as a non-root initially (`set_node_root(node, false)`).
 * - Initializes the number of keys in the internal node to `0` since it's newly created.
 * - This function is typically called when a new internal node is allocated in the tree.
 *
 * Example:
 * ```
 * void *new_node = get_page(pager, new_page_num);
 * initialize_internal_node(new_node);
 * ```
 */
void initialize_internal_node(void *node)
{
    set_node_type(node, INTERNAL_NODE);
    set_node_root(node, false);
    *internal_node_num_keys(node) = 0;
    *node_parent(node) = 0;
    /*
    Necessary because the root page number is 0; by not initializing an internal
    node's right child to an invalid page number when initializing the node, we may
    end up with 0 as the node's right child, which makes the node a parent of the root
    */
    *internal_node_right_child(node) = INVALID_PAGE_NUM;
}

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
        printf("Tried to fetch page total page. %d > %d", page_num, TABLE_MAX_PAGES);
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
 * @brief Retrieves the maximum key from a given B-tree node.
 *
 * Functionality:
 *      - If the node is a **leaf node**, it returns the last key in the node.
 *      - If the node is an **internal node**, it follows the rightmost child
 *        recursively to find the largest key in the subtree.
 *      - This is useful for maintaining B-tree ordering and for operations like
 *        key updates in parent nodes.
 *
 * @param pager A pointer to the `Pager`, which manages page access.
 * @param node A pointer to the current B-tree node.
 *
 * @return `uint32_t` The maximum key value found in the node.
 *
 * Example Usage:
 * ```
 * uint32_t max_key = get_node_max_key(pager, root_node);
 * printf("Maximum key in the tree: %d\n", max_key);
 * ```
 */
uint32_t get_node_max_key(Pager *pager, void *node)
{
    if (get_node_type(node) == LEAF_NODE)
    {
        return *leaf_node_key(node, *leaf_node_num_cells(node) - 1);
    }
    void *right_child = get_page(pager, *internal_node_right_child(node));
    return get_node_max_key(pager, right_child);
}

/**
 * @brief Searches for a key within a leaf node using binary search.
 *
 * This function performs a binary search on a leaf node to locate a given key.
 * If the key is found, it returns a cursor pointing to its position.
 * If the key is not found, the cursor points to the position where
 * the key would be inserted.
 *
 * @param table Pointer to the table containing the B-tree.
 * @param page_num Page number of the leaf node to search.
 * @param key The key to find in the leaf node.
 * @return Cursor* A pointer to a newly allocated cursor pointing to
 *         the key's position in the leaf node.
 */
Cursor *find_leaf_node(Table *table, uint32_t page_num, uint32_t key)
{
    void *node = get_page(table->pager, page_num);
    uint32_t num_cell = *leaf_node_num_cells(node);

    Cursor *cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = page_num;

    uint32_t min_index = 0;
    uint32_t one_past_max_index = num_cell;

    // check until min_index equals to one_past_max_index
    while (min_index != one_past_max_index)
    {
        uint32_t mid_index = (min_index + one_past_max_index) / 2;
        uint32_t key_at_mid_index = *leaf_node_key(node, mid_index);
        if (key_at_mid_index == key)
        {
            cursor->cell_num = mid_index;
            return cursor;
        }
        if (key < key_at_mid_index)
            one_past_max_index = mid_index;
        else
            min_index = mid_index + 1;
    }
    cursor->cell_num = min_index;
    return cursor;
}

/**
 * @brief Finds the appropriate child index in an internal B-tree node for a given key.
 *
 * Functionality:
 *      - This function performs a **binary search** within an internal node
 *        to determine the correct child index that should be followed for a given key.
 *      - It compares the key against stored keys and returns the **index**
 *        where traversal should continue.
 *      - This is essential for efficient tree navigation during searches and insertions.
 *
 * @param node A pointer to the internal B-tree node.
 * @param key The key being searched for.
 *
 * @return `uint32_t` The index of the child pointer that should be followed.
 *
 * Example Usage:
 * ```
 * uint32_t child_index = internal_node_find_child(parent_node, search_key);
 * uint32_t child_page_num = *internal_node_child(parent_node, child_index);
 * ```
 */
uint32_t internal_node_find_child(void *node, uint32_t key)
{
    uint32_t num_keys = *(internal_node_num_keys(node));

    /* Binary search to find index of child to search */
    uint32_t min_index = 0;
    uint32_t max_index = num_keys;

    while (min_index != max_index)
    {
        uint32_t index = (min_index + max_index) / 2;
        uint32_t key_at_index = *(internal_node_key(node, index));
        if (key <= key_at_index)
            max_index = index;
        else
            min_index = index + 1;
    }
    return min_index;
}

/**
 * @brief Updates a key in an internal B-tree node.
 *
 * Functionality:
 *      - Finds the index of an existing key (`old_key`) within the internal node.
 *      - Replaces the key at that index with the **new key** (`new_key`).
 *      - This is useful when a child node's maximum key changes, ensuring
 *        that the parent node maintains correct key references.
 *
 * @param node A pointer to the internal B-tree node.
 * @param old_key The key that needs to be updated.
 * @param new_key The new key to replace the old one.
 *
 * @return `uint32_t*` A pointer to the updated key's location in memory.
 *
 * Example Usage:
 * ```
 * update_internal_node_key(parent_node, old_max_key, new_max_key);
 * ```
 */
uint32_t *update_internal_node_key(void *node, uint32_t old_key, uint32_t new_key)
{
    uint32_t old_child_index = internal_node_find_child(node, old_key);
    *internal_node_key(node, old_child_index) = new_key;
}

/**
 * @brief  Finds the correct child node to traverse in an internal node using binary search.
 *
 * Functionality:
 *      - Retrieves the internal node from the given page number.
 *      - Uses **binary search** to find the correct child pointer for the key.
 *      - If the key is **less than or equal** to the key at the current index, search in the left half.
 *      - If the key is **greater**, search in the right half.
 *      - Returns a cursor pointing to the correct child node that should contain the key.
 *
 * @param table A pointer to the table structure containing the B-tree.
 * @param page_num The page number of the internal node to search in.
 * @param key The key being searched for.
 *
 * @return `Cursor*` A pointer to a Cursor representing the correct child node.
 *
 * Example Usage:
 * ```
 * Cursor *cursor = find_internal_node(table, root_page_num, key);
 * ```
 */
Cursor *find_internal_node(Table *table, uint32_t page_num, uint32_t key)
{
    void *node = get_page(table->pager, page_num);

    uint32_t child_index = internal_node_find_child(node, key);
    // remember that the children of an internal node can be either leaf nodes or more internal nodes
    uint32_t child_page_num = *internal_node_child(node, child_index);
    void *child = get_page(table->pager, child_page_num);
    switch (get_node_type(child))
    {
    case LEAF_NODE:
        return find_leaf_node(table, child_page_num, key);
    case INTERNAL_NODE:
        return find_internal_node(table, child_page_num, key);
    }
}

/**
 * @brief Finds the cursor pointing to the position of a given key in the table.
 *
 * This function searches for the specified key within the table's B-tree.
 * If the root node is a leaf node, it delegates the search to `find_leaf_node`.
 * If the root node is an internal node, the function currently exits
 * with an error message, as internal node searching is not implemented.
 *
 * @param table Pointer to the table structure containing the B-tree.
 * @param key The key to search for in the table.
 * @return Cursor* Pointer to the cursor indicating the key's location in the table.
 *         If the key is not found, the cursor points to the position where
 *         the key would be inserted.
 */
Cursor *find_table(Table *table, uint32_t key)
{
    uint32_t root_page_num = table->root_page_num;
    void *root_node = get_page(table->pager, root_page_num);

    if (get_node_type(root_node) == LEAF_NODE)
    {
        return find_leaf_node(table, root_page_num, key);
    }
    else
    {
        return find_internal_node(table, root_page_num, key);
    }
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
    Cursor *cursor = find_table(table, 0);

    void *node = get_page(table->pager, cursor->page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);
    cursor->end_of_table = (num_cells == 0);

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
    {
        /* Advance to next leaf node */
        uint32_t next_page_num = *leaf_node_next_leaf(node);
        if (next_page_num == 0)
        {
            /* This was rightmost leaf */
            cursor->end_of_table = true;
        }
        else
        {
            cursor->page_num = next_page_num;
            cursor->cell_num = 0;
        }
    }
}

// This function print select result.
void print_row(Row *row)
{
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
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
                  O_RDWR |      // Open for reading and writing
                      O_CREAT | // Create file if it does not exist
                      O_BINARY, // Binary mode (no CRLF conversions) forgot to include this almost crashed o my windows fuck windows check bug.txt for updates (lol).
                  S_IWUSR |     // User write permission
                      S_IRUSR   // User read permission
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
        printf("%d\n", file_length);
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
        set_node_root(root, true);
        *node_parent(root) = 0;
    }

    return table;
}

uint32_t get_unused_page_num(Pager *pager)
{
    return pager->num_pages;
}

void create_new_root(Table *table, uint32_t right_child_page_num)
{
    void *root = get_page(table->pager, table->root_page_num);
    void *root_right_child = get_page(table->pager, right_child_page_num);
    uint32_t left_child_page_num = get_unused_page_num(table->pager);
    void *left_child = get_page(table->pager, left_child_page_num); // load an unused page from memory

    if (get_node_type(root) == INTERNAL_NODE)
    {
        initialize_internal_node(root_right_child);
        initialize_internal_node(left_child);
    }

    // Ensure left_child's metadata is correctly set after memcpy
    if (get_node_type(left_child) == LEAF_NODE) {
        *leaf_node_num_cells(left_child) = LEAF_NODE_LEFT_SPLIT_COUNT;
    }

    // Left child has data copied from old root
    memcpy(left_child, root, PAGE_SIZE);
    set_node_root(left_child, false);

    if (get_node_type(left_child) == INTERNAL_NODE)
    {
        void *child;
        for (int i = 0; i < *internal_node_num_keys(left_child); i++)
        {
            child = get_page(table->pager, *internal_node_child(left_child, i));
            *node_parent(child) = left_child_page_num;
        }
    }

    // Root node is a new internal node with one key and two children
    initialize_internal_node(root);
    set_node_root(root, true);
    *internal_node_num_keys(root) = 1;
    *internal_node_child(root, 0) = left_child_page_num;
    uint32_t left_child_max_key = get_node_max_key(table->pager, left_child);
    *internal_node_key(root, 0) = left_child_max_key;
    *internal_node_right_child(root) = right_child_page_num;
    *node_parent(left_child) = table->root_page_num;
    *node_parent(root_right_child) = table->root_page_num;
}

void internal_node_split_insert(Table *table, uint32_t parent_page_num,
                                    uint32_t child_page_num);

/**
 * @brief Inserts a new child node into an internal B-tree node.
 *
 * Functionality:
 *      - Retrieves the parent and child nodes from the table's pager.
 *      - Determines the correct position (`index`) for inserting the new child node.
 *      - Updates the parent node's key count.
 *      - If the node exceeds `INTERNAL_NODE_MAX_CELL`, a split should occur (not implemented here).
 *      - Updates the rightmost child if necessary, or shifts existing keys and child pointers.
 *      - Sets the child's parent reference to maintain tree integrity.
 *
 * @param table A pointer to the table structure containing the B-tree.
 * @param parent_page_num The page number of the parent node where insertion will occur.
 * @param child_page_num The page number of the child node being inserted.
 *
 * @return void This function modifies the internal node structure in-place.
 *
 * Example Usage:
 * ```
 * internal_node_insert(table, parent_page_num, new_child_page_num);
 * ```
 */
void internal_node_insert(Table *table, uint32_t parent_page_num, uint32_t child_page_num)
{
    void *parent = get_page(table->pager, parent_page_num);
    void *child = get_page(table->pager, child_page_num);

    uint32_t child_max_key = get_node_max_key(table->pager, child);

    // Find the correct index in the parent node where the new child should be inserted
    uint32_t index = internal_node_find_child(parent, child_max_key);

    // Store the original number of keys in the parent node
    uint32_t orignal_num_keys = *internal_node_num_keys(parent);

    // Check if the internal node has reached its maximum number of keys
    if (orignal_num_keys >= INTERNAL_NODE_MAX_CELL)
    {
        internal_node_split_insert(table, parent_page_num, child_page_num);
        return;
    }
    // Retrieve the rightmost child of the parent node
    uint32_t right_child_page_num = *internal_node_right_child(parent);

    if (right_child_page_num == INVALID_PAGE_NUM)
    {
        *internal_node_right_child(parent) = child_page_num;
        return;
    }
    void *right_child = get_page(table->pager, right_child_page_num);

    /*  If we are already at the max number of cells for a node, we cannot increment
        before splitting. Incrementing without inserting a new key/child pair
        and immediately calling internal_node_split_and_insert has the effect
        of creating a new key at (max_cells + 1) with an uninitialized value
    */
    *internal_node_num_keys(parent) = orignal_num_keys + 1;
    /*
    If the new child’s max key is greater than the rightmost child’s max key,
    update the rightmost child pointer

    we are ensuring that a child node inserted into an empty internal node will become that internal node’s right child without any other operations being performed, since an empty internal node has no keys to manipulate.
    */
    if (child_max_key > get_node_max_key(table->pager, right_child))
    {
        // Move the current right child to the last cell
        *internal_node_child(parent, orignal_num_keys) = right_child_page_num;
        *internal_node_key(parent, orignal_num_keys) = get_node_max_key(table->pager, right_child);
        // Set the new child as the rightmost child
        *internal_node_right_child(parent) = child_page_num;
    }
    else
    {
        // Make space for the new cell
        for (uint32_t i = orignal_num_keys; i > index; i--)
        {
            void *destination = internal_node_cell(parent, i);
            void *source = internal_node_cell(parent, i - 1);
            memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
        }
        // Insert the new child at the correct position
        *internal_node_child(parent, index) = child_page_num;
        *internal_node_key(parent, index) = child_max_key;
    }

    // Update the child's parent pointer to the parent node
    void *child_node = get_page(table->pager, child_page_num);
    *node_parent(child_node) = parent_page_num;
}

void internal_node_split_insert(Table *table, uint32_t parent_page_num, uint32_t child_page_num)
{
    uint32_t old_page_num = parent_page_num;
    void *old_node = get_page(table->pager, old_page_num);
    uint32_t old_max = get_node_max_key(table->pager, old_node);

    void *child = get_page(table->pager, child_page_num);
    uint32_t child_max = get_node_max_key(table->pager, child);

    uint32_t new_page_num = get_unused_page_num(table->pager);

    /*
    Declaring a flag before updating pointers which
    records whether this operation involves splitting the root -
    if it does, we will insert our newly created node during
    the step where the table's new root is created. If it does
    not, we have to insert the newly created node into its parent
    after the old node's keys have been transferred over. We are not
    able to do this if the newly created node's parent is not a newly
    initialized root node, because in that case its parent may have existing
    keys aside from our old node which we are splitting. If that is true, we
    need to find a place for our newly created node in its parent, and we
    cannot insert it at the correct index if it does not yet have any keys
    */

    uint32_t splitting_root = is_root_node(old_node);
    void *parent;
    void *new_node;
    if (splitting_root)
    {
        create_new_root(table, new_page_num);
        parent = get_page(table->pager, table->root_page_num);
        /*
        If we are splitting the root, we need to update old_node to point
        to the new root's left child, new_page_num will already point to
        the new root's right child
        */
        old_page_num = *internal_node_child(parent, 0);
        old_node = get_page(table->pager, old_page_num);
    }
    else
    {
        parent = get_page(table->pager, *node_parent(old_node));
        new_node = get_page(table->pager, new_page_num);
        initialize_internal_node(new_node);
    }
    uint32_t *old_num_keys = internal_node_num_keys(old_node);

    uint32_t cur_page_num = *internal_node_right_child(old_node); // beacause every node is filled completly
    void *curr = get_page(table->pager, cur_page_num);

    // First put right child into new node and set right child of old node to invalid page number
    internal_node_insert(table, new_page_num, cur_page_num);
    *node_parent(curr) = new_page_num;
    *internal_node_right_child(old_node) = INVALID_PAGE_NUM;

    // For each key until you get to the middle key, move the key and the child to the new node
    for (int i = INTERNAL_NODE_MAX_CELL - 1; i > INTERNAL_NODE_MAX_CELL / 2; i--)
    {
        cur_page_num = *internal_node_child(old_node, i);
        curr = get_page(table->pager, cur_page_num);

        internal_node_insert(table, new_page_num, cur_page_num);
        *node_parent(curr) = new_page_num;
        (*old_num_keys)--;
    }
    // Set child before middle key, which is now the highest key, to be node's right child, and decrement number of keys
    *internal_node_right_child(old_node) = *internal_node_child(old_node, *old_num_keys - 1);
    (*old_num_keys)--;

    // Determine which of the two nodes after the split should contain the child to be inserted, and insert the child
    uint32_t max_after_split = get_node_max_key(table->pager, old_node);

    uint32_t destination_page_num = child_max < max_after_split ? old_page_num : new_page_num;

    internal_node_insert(table, destination_page_num, child_page_num);
    *node_parent(child) = destination_page_num;

    update_internal_node_key(parent, old_max, get_node_max_key(table->pager, old_node));

    if (!splitting_root)
    {
        internal_node_insert(table, *node_parent(old_node), new_page_num);
        *node_parent(new_node) = *node_parent(old_node);
    }
}

/**
 * Splits a full leaf node and inserts a new key-value pair into the appropriate split node.
 *
 * @param cursor Pointer to the cursor indicating the insertion position.
 * @param key The key to insert into the leaf node.
 * @param value Pointer to the row data to be stored in the leaf node.
 *
 * @note This function handles the case when a leaf node is full and needs to be split.
 *       It creates a new node, redistributes existing keys between the old and new node,
 *       inserts the new key in the appropriate location, and updates the parent if needed.
 */
void leaf_node_split_insert(Cursor *cursor, uint32_t key, Row *value)
{
    void *old_node = get_page(cursor->table->pager, cursor->page_num); // Get the current full leaf node
    uint32_t old_max = get_node_max_key(cursor->table->pager, old_node);
    // Allocate a new page for the split node
    uint32_t new_page_num = get_unused_page_num(cursor->table->pager); // load an unused page from memory
    void *new_node = get_page(cursor->table->pager, new_page_num);
    initialize_leaf_node(new_node); // Initialize the new leaf node
    // we are copying parent of old node to new_node as they will have sme parent
    *node_parent(new_node) = *node_parent(old_node);
    // give whatever is stored in old_node next leaf to new_node next leaf.
    *leaf_node_next_leaf(new_node) = *leaf_node_next_leaf(old_node);
    *leaf_node_next_leaf(old_node) = new_page_num; // old node points to new node.

    // Redistribute keys between the old and new node
    for (int32_t i = LEAF_NODE_MAX_CELL; i >= 0; i--)
    {
        void *destination_node;

        // Determine if the key should go to the new or old node
        if (i >= LEAF_NODE_LEFT_SPLIT_COUNT)
            destination_node = new_node; // upper half
        else
            destination_node = old_node; // lower half

        // Get the index within the respective node
        uint32_t index_within_node = i % LEAF_NODE_LEFT_SPLIT_COUNT;
        void *destination = leaf_node_cell(destination_node, index_within_node);

        /*
        To better understand this logic below is example:
            Assume the following initial leaf node (max 4 cells), and we want to insert key 25 at index 2:
                  Index	    Old Keys Before Insert	    New Keys After Insert
                    0	            10	                        10
                    1	            20	                        20
                    2	            30	                        25 (Inserted)
                    3	            40	                        30
                    4	            50	                        40
                    5	        (Overflow)	                    50
        */
        if (i == cursor->cell_num)
        {
            serialize_row(value, leaf_node_value(destination_node, index_within_node)); // Insert the new key-value pair at the correct location
            *leaf_node_key(destination_node, index_within_node) = key;
        }
        else if (i > cursor->cell_num)
            memcpy(destination, leaf_node_cell(old_node, i - 1), LEAF_NODE_CELL_SIZE); // Shift right
        else
            memcpy(destination, leaf_node_cell(old_node, i), LEAF_NODE_CELL_SIZE); // Copy existing
    }
    // Update the number of cells in both nodes after splitting
    *(leaf_node_num_cells(old_node)) = LEAF_NODE_LEFT_SPLIT_COUNT;
    *(leaf_node_num_cells(new_node)) = LEAF_NODE_RIGHT_SPLIT_COUNT;

    // If the old node was the root, create a new root
    if (is_root_node(old_node))
        return create_new_root(cursor->table, new_page_num);
    else
    {
        uint32_t parent_page_num = *node_parent(old_node);
        uint32_t new_max = get_node_max_key(cursor->table->pager, old_node);
        void *parent = get_page(cursor->table->pager, parent_page_num);

        update_internal_node_key(parent, old_max, new_max);
        internal_node_insert(cursor->table, parent_page_num, new_page_num);
        return;
    }
}

/**
 * Inserts a key-value pair into a leaf node at the specified cursor position.
 * If the node is full, it prints a message and exits (branching not implemented yet).
 *
 * @param cursor Pointer to the cursor indicating the insertion position.
 * @param key The key to insert into the leaf node.
 * @param value Pointer to the row data to be stored in the leaf node.
 *
 * @note If the insertion position is not at the end, existing entries are shifted to make space.
 * @note If the leaf node is full, the function exits with an error message.
 */
void leaf_node_insert(Cursor *cursor, uint32_t key, Row *value)
{
    void *node = get_page(cursor->table->pager, cursor->page_num);

    uint32_t num_cell = *leaf_node_num_cells(node);
    if (num_cell >= LEAF_NODE_MAX_CELL)
    {
        leaf_node_split_insert(cursor, key, value);
        return; // resaon for bug there wasnt return here
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
    printf("LEAF_NODE_LEFT_SPLIT_COUNT: %d\n", LEAF_NODE_LEFT_SPLIT_COUNT);
    printf("LEAF_NODE_RIGHT_SPLIT_COUNT: %d\n", LEAF_NODE_RIGHT_SPLIT_COUNT);
}

void indent(uint32_t level)
{
    for (uint32_t i = 0; i < level; i++)
        printf(" ");
}

void print_tree(Pager *pager, uint32_t page_num, uint32_t indentation_level)
{
    void *node = get_page(pager, page_num);
    uint32_t num_keys, child;

    switch (get_node_type(node))
    {
    case (LEAF_NODE):
        num_keys = *leaf_node_num_cells(node);
        indent(indentation_level);
        printf("- leaf (size %d)\n", num_keys);
        for (uint32_t i = 0; i < num_keys; i++)
        {
            indent(indentation_level + 1);
            printf("- %d\n", *leaf_node_key(node, i));
        }
        break;
    case (INTERNAL_NODE):
        num_keys = *internal_node_num_keys(node);
        indent(indentation_level);
        printf("- internal (size %d)\n", num_keys);
        if (num_keys > 0)
        {
            for (uint32_t i = 0; i < num_keys; i++)
            {
                child = *internal_node_child(node, i);
                print_tree(pager, child, indentation_level + 1);

                indent(indentation_level + 1);
                printf("- key %d\n", *internal_node_key(node, i));
            }
            child = *internal_node_right_child(node);
            print_tree(pager, child, indentation_level + 1);
        }
        break;
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
        print_tree(table->pager, 0, 0);
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

PrepareResult preare_update(InputBuffer *input_buffer, Statement *statement)
{
    statement->type = STATEMENT_UPDATE;

    // Tokenizing input string
    char *update_keyword = strtok(input_buffer->buffer, " ");
    char *username = strtok(NULL, " ");
    char *email = strtok(NULL, " ");
    char *where_keyword = strtok(NULL, " ");
    char *id_token = strtok(NULL, " "); // Should contain "id=1"

    // Validate required keywords
    if (update_keyword == NULL || username == NULL || email == NULL || where_keyword == NULL || id_token == NULL)
    {
        return PREPARE_SYNTAX_ERROR;
    }

    // Ensure 'where' clause is correctly formatted
    if (strcmp(where_keyword, "where") != 0)
    {
        return PREPARE_SYNTAX_ERROR;
    }

    // Split "id=1" into "id" and "1"
    char *id_key = strtok(id_token, "=");
    char *id_value = strtok(NULL, "=");

    if (id_key == NULL || id_value == NULL || strcmp(id_key, "id") != 0)
    {
        return PREPARE_SYNTAX_ERROR;
    }

    // Convert ID to integer
    int id = atoi(id_value);
    if (id < 0)
        return PREPARE_NEGATIVE_ID;

    // Validate username and email length
    if (strlen(username) > COLUMN_USERNAME_SIZE || strlen(email) > COLUMN_EMAIL_SIZE)
    {
        return PREPARE_STRING_TOO_LONG;
    }

    // Assign values to the statement
    statement->row_to_insert.id = id;
    strcpy(statement->row_to_insert.username, username);
    strcpy(statement->row_to_insert.email, email);

    return PREPARE_SUCCESS;
}

PrepareResult prepare_delete(InputBuffer *input_buffer, Statement *statement)
{
    statement->type = STATEMENT_DELETE;

    char *delete_keyword = strtok(input_buffer->buffer, " ");
    char *where_keyword = strtok(NULL, " ");
    char *id_token = strtok(NULL, " ");
    // Validate required keywords
    if (delete_keyword == NULL || where_keyword == NULL || id_token == NULL)
    {
        return PREPARE_SYNTAX_ERROR;
    }

    char *id_key = strtok(id_token, "=");
    char *id_value = strtok(NULL, "=");

    if (id_key == NULL || id_value == NULL || strcmp(id_key, "id") != 0)
    {
        return PREPARE_SYNTAX_ERROR;
    }

    // Convert ID to integer
    int id = atoi(id_value);
    if (id < 0)
        return PREPARE_NEGATIVE_ID;

    statement->row_to_insert.id = id;
    return PREPARE_SUCCESS;
}

PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *statement)
{
    if (strncmp(input_buffer->buffer, "insert", 6) == 0)
    {
        return prepare_insert(input_buffer, statement);
    }
    else if (strncmp(input_buffer->buffer, "update", 6) == 0)
    {
        return preare_update(input_buffer, statement);
    }
    else if (strncmp(input_buffer->buffer, "delete", 6) == 0)
    {
        return prepare_delete(input_buffer, statement);
    }
    else if (strcmp(input_buffer->buffer, "select") == 0)
    {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    else
        return PREPARE_UNRECOGNIZED_STATEMENT;
}

ExecuteResult execute_insert(Statement *statement, Table *table)
{

    Row *row_to_insert = &(statement->row_to_insert);
    uint32_t key_to_insert = row_to_insert->id;
    Cursor *cursor = find_table(table, key_to_insert);

    void *node = get_page(cursor->table->pager, cursor->page_num);
    uint32_t num_cells = *leaf_node_num_cells(node);

    if (cursor->cell_num < num_cells)
    {
        uint32_t key_at_index = *leaf_node_key(node, cursor->cell_num);
        if (key_at_index == key_to_insert)
            return EXECUTE_DUPLICATE_KEY;
    }

    leaf_node_insert(cursor, row_to_insert->id, row_to_insert);

    free(cursor);
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_update(Statement *statement, Table *table)
{
    Row *row_to_update = &(statement->row_to_insert);
    uint32_t key_to_update = row_to_update->id;
    Cursor *cursor = find_table(table, key_to_update);

    void *node = get_page(cursor->table->pager, cursor->page_num);

    void *row_location = leaf_node_value(node, cursor->cell_num);

    memcpy(row_location + USERNAME_OFFSET, row_to_update->username, USERNAME_SIZE);
    memcpy(row_location + EMAIL_OFFSET, row_to_update->email, EMAIL_SIZE); // same here

    free(cursor);
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_delete(Statement *statement, Table *table)
{
    Row *row_to_delete = &(statement->row_to_insert);
    uint32_t row_key = row_to_delete->id;
    Cursor *cursor = find_table(table, row_key);

    void *node = get_page(cursor->table->pager, cursor->page_num);
    uint32_t num_cell = *(leaf_node_num_cells(node));

    // Check if the cursor points to a valid cell
    if (cursor->cell_num >= num_cell)
    {
        printf("Error: No row found with id %d\n", row_key);
        free(cursor);
        return EXECUTE_NOT_FOUND; // Or define a new error type
    }

    // Shift all subsequent rows left to fill the gap (for imagination consider it an array of size num_cell)
    for (uint32_t i = cursor->cell_num; i < num_cell - 1; i++)
    {
        memcpy(
            leaf_node_cell(node, i),     // Destination: current cell
            leaf_node_cell(node, i + 1), // Source: next cell
            LEAF_NODE_CELL_SIZE);
    }
    // Reduce the count of stored rows
    (*leaf_node_num_cells(node))--;
    printf("deleted %d\n", row_key);
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
    case (STATEMENT_UPDATE):
        return execute_update(statement, table);
    case (STATEMENT_DELETE):
        return execute_delete(statement, table);
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
        case (EXECUTE_DUPLICATE_KEY):
            printf("Duplicate key found.\n");
            break;
        case (EXECUTE_TABLE_FULL):
            printf("Error Table full.\n");
            break;
        }
    }
    return 0;
}