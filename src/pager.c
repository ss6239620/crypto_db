#include "constants.h"

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

uint32_t get_unused_page_num(Pager *pager)
{
    return pager->num_pages;
}
