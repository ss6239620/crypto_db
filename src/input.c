#include "constants.h"

// Intialize new input buffer
InputBuffer *new_input_buffer()
{
    InputBuffer *input_buffer = (InputBuffer *)malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;
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

