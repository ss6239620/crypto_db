#include "constants.h"

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