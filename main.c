#include "src/btree.c"
#include "src/constants.h"
#include "src/cursor.c"
#include "src/db.c"
#include "src/input.c"
#include "src/internal_node.c" 
#include "src/leaf_node.c" 
#include "src/pager.c" 
#include "src/query_processing.c" 
#include "src/test.c"

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
