#include "constants.h"

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

