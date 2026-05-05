#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>


#define CHAT_PATH_TEMPLATE "/dev/telegram/chat_%d"
#define BUFFER_SIZE 4096


void print_usage(const char* prog_name) {
    printf("Usage:\n");
    printf("  %s read <chat_id>          - read messages\n", prog_name);
    printf("  %s write <chat_id> <text>  - send a message\n", prog_name);
}

int handle_read(int chat_id) {
    char path[64];
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    snprintf(path, sizeof(path), CHAT_PATH_TEMPLATE, chat_id);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open chat");
        return 1;
    }

    while ((bytes_read = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("%s", buffer);
    }

    if (bytes_read < 0) {
        perror("Error reading chat");
    }

    close(fd);
    return 0;
}

int handle_write(int chat_id, const char* text) {
    char path[64];
    snprintf(path, sizeof(path), CHAT_PATH_TEMPLATE, chat_id);

    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open chat");
        return 1;
    }

    ssize_t bytes_written = write(fd, text, strlen(text));

    if (bytes_written < 0) {
        if (errno == EMSGSIZE) {
            fprintf(stderr, "Error: message too long (limit 1024 characters)\n");
        } else {
            perror("Error writing to chat");
        }
        close(fd);
        return 1;
    }

    printf("Message sent successfully!\n");
    close(fd);
    return 0;
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    int chat_id = atoi(argv[2]);
    if (chat_id < 1 || chat_id > 5) {
        fprintf(stderr, "Invalid chat ID. Available IDs are from 1 to 5.\n");
        return 2;
    }

    if (strcmp(argv[1], "read") == 0) {
        return handle_read(chat_id);
    }
    else if (strcmp(argv[1], "write") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Message text not specified.\n");
            return 3;
        }

        return handle_write(chat_id, argv[3]);
    }
    else {
        print_usage(argv[0]);
        return 1;
    }
}
