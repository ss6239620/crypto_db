#include "constants.h"

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
