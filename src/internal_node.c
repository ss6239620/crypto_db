#include "constants.h"

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
