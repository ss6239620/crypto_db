#include "constants.h"

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
    if (get_node_type(left_child) == LEAF_NODE)
    {
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
