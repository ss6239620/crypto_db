#include "constants.h"

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

void print_prompt()
{
    printf("crypto> ");
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
