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

struct Pager_t
{
    int file_descriptor;          // 4 bytes
    uint32_t file_length;         // 4 bytes
    void *pages[TABLE_MAX_PAGES]; // 100 pointers = (void *) 8 bytes * 100 = 800 bytes
};
typedef struct Pager_t Pager;

struct Table_t
{
    Pager *pager;
    uint32_t num_rows;
};
typedef struct Table_t Table;

// This function print select result.
void print_row(Row *row)
{
    printf("%d, %s, %s", row->id, row->username, row->email);
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
    }
    return pager->pages[page_num];
}

/*
The row_slot function calculates the memory location of a specific row in a table. It uses paging to locate the correct memory page and then determines the exact byte offset of the row within that page.
    Parameters:
        table: A pointer to a Table structure, which holds the database rows.
        row_num: The row number that we need to retrieve.
    return value:
    Returns a pointer to the location in memory where the specified row is stored.
 */
void *row_slot(Table *table, uint32_t row_num)
{
    uint32_t page_num = row_num / ROWS_PER_PAGE;   // Determine the Page Number
    void *page = get_page(table->pager, page_num); // Fetch the Page from the Pager
    uint32_t row_offset = row_num % ROWS_PER_PAGE; // Calculate Row Offset Within the Page
    /*
    Byte offset: The byte offset is the distance (in bytes) from the beginning of a page to the start of the desired row. It is not where the row ends, but rather where it begins within the page.

    Calculate the Byte Offset
        ROW_SIZE represents the number of bytes occupied by a single row.
        This calculation determines how far into the page the row is stored.
        Example: If ROW_SIZE = 200 bytes and row_offset = 2 then byte_offset = 2 * 200 = 400
                Row 0 starts at 0 * ROW_SIZE = 0
                Row 1 starts at 1 * ROW_SIZE = 200
                Row 2 starts at 2 * ROW_SIZE = 400
    */
    uint32_t byte_offset = row_offset * ROW_SIZE;
    return page + byte_offset;
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

    //  Initializing Page Pointers as NULL
    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++)
        pager->pages[i] = NULL;

    return pager;
}

// Open a Database connection and pepare the table
Table *db_open(const char *filename)
{
    Pager *pager = page_open(filename);
    // Find Intial number of rows present inside db file
    uint32_t num_rows = pager->file_length / ROW_SIZE;

    Table *table = (Table *)malloc(sizeof(Table));
    table->pager = pager;
    table->num_rows = num_rows;

    return table;
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

/*
The function pager_flush is responsible for writing a specific page from memory to a file on disk. This function ensures that data stored in memory (inside pager->pages[]) is saved to the database file before exiting or continuing execution.
Parameters:
    pager: A pointer to a Pager struct, which manages the database file.
    page_num: The index of the page to be flushed (written) to disk.
    size: The number of bytes to write to the file.
*/
void pager_flush(Pager *pager, uint32_t page_num, uint32_t size)
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
    ssize_t byte_written = write(pager->file_descriptor, pager->pages[page_num], size);

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
    Pager *pager = table->pager;                               // Pointer to the table
    uint32_t num_full_pages = table->num_rows / ROWS_PER_PAGE; // Total number of pages which have rows

    for (uint32_t i = 0; i < num_full_pages; i++)
    {
        if (pager->pages[i] == NULL)
            continue;
        pager_flush(pager, i, PAGE_SIZE); // save to db
        free(pager->pages[i]);
        pager->pages[i] = NULL;
    }
    // There may be a partial page to write to the end of the file
    uint32_t num_additional_rows = table->num_rows % ROWS_PER_PAGE;
    if (num_additional_rows > 0)
    {
        uint32_t num_page = num_full_pages; // As num_full_pages will have the last page
        if (pager->pages[num_page] != NULL)
        {
            pager_flush(pager, num_page, num_additional_rows * ROW_SIZE); // save to db
            free(pager->pages[num_page]);
            pager->pages[num_page] = NULL;
        }
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

MetaCommandResult do_meta_command(InputBuffer *input_buffer, Table *table)
{
    if (strcmp(input_buffer->buffer, ".exit") == 0)
    {
        db_close(table);
        exit(EXECUTE_SUCCESS);
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
        printf("heheh");
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
    if (table->num_rows >= TABLE_MAX_ROWS)
        return EXECUTE_TABLE_FULL;

    Row *row_to_insert = &(statement->row_to_insert);
    serialize_row(row_to_insert, row_slot(table, table->num_rows));
    table->num_rows += 1;

    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement *statement, Table *table)
{
    Row row; // if we use Row *row than memory is unintialized and program may crash to avoid this using Row row
    for (uint32_t i = 0; i < table->num_rows; i++)
    {
        deserialize_row(row_slot(table, i), &row);
        print_row(&row);
    }
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