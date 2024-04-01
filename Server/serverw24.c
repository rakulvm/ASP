#define _XOPEN_SOURCE 700  // For broader POSIX compatibility, including nftw and DT_DIR
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>  // For strcasecmp
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <dirent.h>  // For DT_DIR
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <ctype.h>
#include <time.h>
#include <ftw.h>  // For nftw
#include <glob.h>   // For glob() function

#define BUFFER_SIZE 256
#define PORT_NO 2024
#define MAX_DIRS 512

#define MAX_MATCHING_FILES 1000 // Adjust based on expected server load and use case

struct {
    char *files[MAX_MATCHING_FILES];
    int count;
} matchingFiles;

static FILE* tarFile;

typedef struct {
    char *name;
    time_t mod_time;
} DirEntry;


// Global struct to store file search criteria and results
struct fileInfo {
    char path[BUFFER_SIZE * 10]; // Increased size to hold file paths
    long size1;
    long size2;
    int found;
} fileInfo;


void error(const char *msg) {
    perror(msg);
    exit(1);
}

int dir_time_cmp(const void *a, const void *b) {
    const DirEntry* dirA = (const DirEntry*)a;
    const DirEntry* dirB = (const DirEntry*)b;
    return (dirA->mod_time > dirB->mod_time) - (dirA->mod_time < dirB->mod_time);
}

int dir_cmp(const void *a, const void *b) {
    const DirEntry* dirA = (const DirEntry*)a;
    const DirEntry* dirB = (const DirEntry*)b;
    return strcasecmp(dirA->name, dirB->name);
}

void listDirectories(int client_sock_fd, int sort_by_time) {
    char *homeDir = getenv("HOME");
    if (!homeDir) homeDir = ".";
    
    DIR *d = opendir(homeDir);
    struct dirent *dir;
    DirEntry *entries[MAX_DIRS];
    int count = 0;
    char buffer[BUFFER_SIZE];
    struct stat path_stat;
    char timebuff[256]; 
    
    if (d) {
        while ((dir = readdir(d)) != NULL && count < MAX_DIRS) {
            char full_path[BUFFER_SIZE];
            snprintf(full_path, sizeof(full_path), "%s/%s", homeDir, dir->d_name);
            stat(full_path, &path_stat);
            if (S_ISDIR(path_stat.st_mode)) {  // Check if it's a directory using stat
                entries[count] = malloc(sizeof(DirEntry));
                entries[count]->name = strdup(dir->d_name);
                if (stat(full_path, &path_stat) == 0) {
                    entries[count]->mod_time = path_stat.st_mtime;
                } else {
                    entries[count]->mod_time = 0;
                }
                count++;
            }
        }
        closedir(d);    

        // Sort based on the flag 'sort_by_time'
        qsort(entries, count, sizeof(DirEntry *), sort_by_time ? dir_time_cmp : dir_cmp);

        // Send sorted directory names to the client, include timestamps if sorting by time
        for (int i = 0; i < count; i++) {
            if (sort_by_time) {
                struct tm *tm_info = localtime(&(entries[i]->mod_time));
                strftime(timebuff, sizeof(timebuff), "%Y-%m-%d %H:%M:%S", tm_info);
                snprintf(buffer, BUFFER_SIZE, "%s - %s\n", timebuff, entries[i]->name);
            } else {
                snprintf(buffer, BUFFER_SIZE, "%s\n", entries[i]->name);
            }
            if (write(client_sock_fd, buffer, strlen(buffer)) < 0) error("ERROR writing to socket");
            free(entries[i]->name);
            free(entries[i]);
        }
    } else {
        // Could not open directory
        if (write(client_sock_fd, "ERROR opening directory\n", 25) < 0) error("ERROR writing to socket");
    }

    // End of directory list signal
    if (write(client_sock_fd, "\n", 1) < 0) error("ERROR writing to socket");
}


// New function to create the w24project directory and check for errors
int createDirectory(const char *dir) {
    struct stat st = {0};

    if (stat(dir, &st) == -1) {
        if (mkdir(dir, 0777) != 0) {
            return -1;  // Return an error if directory creation fails
        }
    }
    return 0;  // Return success if directory exists or was created successfully
}

// New function to handle the creation of tar.gz file
int createTarFile(const char *dir, const char *tarName) {
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "tar -czf %s/%s -C %s .", dir, tarName, dir);

    return system(command);
}

// Function to count the number of extensions and check for duplicates
int validateExtensions(const char *extensions, int *count) {
    char extCopy[BUFFER_SIZE];
    char *token;
    char *extensionsArray[3] = {NULL, NULL, NULL}; // Supports up to 3 extensions

    strncpy(extCopy, extensions, sizeof(extCopy));
    extCopy[sizeof(extCopy) - 1] = '\0'; // Ensure null-termination

    *count = 0;
    token = strtok(extCopy, " ");
    while (token && *count < 3) {
        // Check for duplicate extensions
        for (int i = 0; i < *count; i++) {
            if (strcmp(extensionsArray[i], token) == 0) {
                // Duplicate extension found
                return -1;
            }
        }
        extensionsArray[*count] = token;
        (*count)++;
        token = strtok(NULL, " ");
    }
    if (token) { // More than 3 extensions provided
        return -2;
    }
    if (*count == 0) { // No extensions provided
        return -3;
    }
    return 0; // All good
}

void packFilesByExtension(int client_sock_fd, const char *extensions) {
    char w24projectDir[BUFFER_SIZE];
    char tarFilePath[BUFFER_SIZE];
    char command[BUFFER_SIZE * 6];
    char extensionsCopy[BUFFER_SIZE]; // Mutable copy of extensions
    int extCount;
    int validationResult = validateExtensions(extensions, &extCount);
    char notification[BUFFER_SIZE];

    // Check validation result and respond appropriately
    switch (validationResult) {
        case -1:
            snprintf(notification, sizeof(notification), "Error: Duplicate file types provided.\n");
            write(client_sock_fd, notification, strlen(notification));
            return;
        case -2:
            snprintf(notification, sizeof(notification), "Error: Number of extensions greater than the limit.\n");
            write(client_sock_fd, notification, strlen(notification));
            return;
        case -3:
            snprintf(notification, sizeof(notification), "Error: No file extensions provided.\n");
            write(client_sock_fd, notification, strlen(notification));
            return;
    }

    // Copy the extensions to a mutable string
    strncpy(extensionsCopy, extensions, sizeof(extensionsCopy));
    extensionsCopy[sizeof(extensionsCopy) - 1] = '\0'; // Ensure null-termination

    snprintf(w24projectDir, sizeof(w24projectDir), "%s/w24project", getenv("HOME") ? getenv("HOME") : ".");
    snprintf(tarFilePath, sizeof(tarFilePath), "%s/temp.tar.gz", w24projectDir);

    // Create w24project directory if it does not exist
    if (createDirectory(w24projectDir) != 0) {
        write(client_sock_fd, "Failed to create project directory.\n", 35);
        return;
    }

    // Cleanup previous tar file if exists
    unlink(tarFilePath);

    // Start building the find command
    strcpy(command, "find ~ -type f \\( ");

    // Tokenize the mutable copy of extensions and build the rest of the find command
    char *token = strtok(extensionsCopy, " ");
    while (token) {
        strcat(command, "-name '*.");
        strcat(command, token);
        strcat(command, "' ");
        token = strtok(NULL, " ");
        if (token) {
            strcat(command, "-o "); // OR operator for multiple extensions
        }
    }

    // Finish building the find command to print null-terminated strings
    strcat(command, "\\) -print0 | tar --null -T - --transform 's/.*\\///' -czvf ");
    strcat(command, tarFilePath);

    // Execute the command
    int result = system(command);
    if (result != 0) {
        write(client_sock_fd, "Failed to find and pack files.\n", 30);
        return;
    }

    // Check if tar file has been created and is not empty
    struct stat tarStat;
    if (stat(tarFilePath, &tarStat) == -1 || tarStat.st_size == 0) {
        write(client_sock_fd, "No matching files found to pack.\n", 32);
        return;
    }

    // Notify the client of successful tar file creation
    // char notification[BUFFER_SIZE];
    snprintf(notification, sizeof(notification), "Files packed into %s\n", tarFilePath);
    write(client_sock_fd, notification, strlen(notification));
}

// Function to be called by nftw for each encountered file
static int file_info(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (typeflag == FTW_F) {
        // If this is a file and it matches the filename we're looking for...
        if (strcmp(fpath + ftwbuf->base, fileInfo.path) == 0) { // Corrected the usage here
            fileInfo.found = 1; // Mark as found
            snprintf(fileInfo.path, sizeof(fileInfo.path), "%s", fpath); // Copy full path
            return 1; // Stop walking the tree
        }
    }
    return 0; // Continue walking the tree
}

void sendFileInfo(int client_sock_fd, char *filename) {
    char buffer[BUFFER_SIZE * 2];
    char timebuff[256];
    struct stat file_stat;

    // Reset found flag and copy filename into global fileInfo structure
    fileInfo.found = 0;  
    strncpy(fileInfo.path, filename, BUFFER_SIZE);

    // Walk through the file tree starting at the user's home directory
    nftw(getenv("HOME"), file_info, 20, FTW_PHYS);

    if (fileInfo.found) {
        // If file is found, stat it to get its information
        if (stat(fileInfo.path, &file_stat) == 0) {
            strftime(timebuff, sizeof(timebuff), "%Y-%m-%d %H:%M:%S", localtime(&file_stat.st_mtime));
            snprintf(buffer, sizeof(buffer), "Filename: %s\nSize: %ld bytes\nDate modified: %s\nPermissions: %o\n",
                     fileInfo.path, file_stat.st_size, timebuff, file_stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
            write(client_sock_fd, buffer, strlen(buffer));
        } else {
            // If stat fails after finding the file
            snprintf(buffer, sizeof(buffer), "Error retrieving file info\n");
            write(client_sock_fd, buffer, strlen(buffer));
        }
    } else {
        // If the file wasn't found
        snprintf(buffer, sizeof(buffer), "File not found\n");
        write(client_sock_fd, buffer, strlen(buffer));
    }

    // End signal to indicate message completion
    snprintf(buffer, sizeof(buffer), "END\n");
    write(client_sock_fd, buffer, strlen(buffer));
}

// Function to handle the 'w24fz' command
void packFilesBySize(int client_sock_fd, long size1, long size2) {
    char w24projectDir[BUFFER_SIZE];
    char fileListPath[BUFFER_SIZE];
    char tarFilePath[BUFFER_SIZE];
    FILE *fp;
    int status;

    snprintf(w24projectDir, sizeof(w24projectDir), "%s/w24project", getenv("HOME") ? getenv("HOME") : ".");
    snprintf(fileListPath, sizeof(fileListPath), "%s/filelist.txt", w24projectDir);
    snprintf(tarFilePath, sizeof(tarFilePath), "%s/temp.tar.gz", w24projectDir);

    // Create w24project directory if it does not exist
    if (createDirectory(w24projectDir) != 0) {
        write(client_sock_fd, "Failed to create project directory.\n", 35);
        return;
    }

    // Cleanup previous files if exist
    unlink(tarFilePath);
    unlink(fileListPath);

    // Find command to locate files within the specified size range and output to a file list
    char findCommand[BUFFER_SIZE * 3];
    snprintf(findCommand, sizeof(findCommand),
             "find ~ -type f \\( -size +%ldc -a -size -%ldc \\) -print > %s",
             size1, size2, fileListPath);

    // Execute the find command
    status = system(findCommand);
    if (status != 0) {
        write(client_sock_fd, "Failed to find files or no files found.\n", 38);
        return;
    }

    // Prepare the tar command
    char tarCommand[BUFFER_SIZE * 3];
    snprintf(tarCommand, sizeof(tarCommand),
             "tar --transform 's#.*/##' -czf %s -T %s",
             tarFilePath, fileListPath);

    // Execute the tar command
    status = system(tarCommand);
    if (status != 0) {
        write(client_sock_fd, "Failed to pack files into tar.\n", 30);
        return;
    }

    // Notify the client of successful tar file creation
    char notification[BUFFER_SIZE];
    snprintf(notification, sizeof(notification), "Files packed into %s\n", tarFilePath);
    write(client_sock_fd, notification, strlen(notification));
}

void crequest(int client_sock_fd) {
    char buffer[BUFFER_SIZE];
    while (1) {  // Infinite loop to handle client commands
        memset(buffer, 0, BUFFER_SIZE); 
        if (read(client_sock_fd, buffer, BUFFER_SIZE - 1) < 0) error("ERROR reading from socket");

        if (strncmp(buffer, "quitc", 5) == 0) {
            printf("Client has requested to close the connection.\n");
            break;  // Exit loop and end child process
        }

        if (strncmp(buffer, "dirlist -a", 10) == 0) {
            listDirectories(client_sock_fd, 0); // 0 for sorting alphabetically
        } else if (strncmp(buffer, "dirlist -t", 10) == 0) {
            listDirectories(client_sock_fd, 1); // 1 for sorting by time
        } else if (strncmp(buffer, "w24fn ", 6) == 0) { // Check if the command is w24fn
             char filename[BUFFER_SIZE];
            strncpy(filename, buffer + 6, BUFFER_SIZE); // Extract the filename from the command
            sendFileInfo(client_sock_fd, filename);
        } else if (strncmp(buffer, "w24ft ", 6) == 0) {
            char extensions[BUFFER_SIZE];
            strcpy(extensions, buffer + 6); // Extract the extensions part from the command
            packFilesByExtension(client_sock_fd, extensions);
        } else if (strncmp(buffer, "w24fz ", 6) == 0) {
            long size1, size2;
            sscanf(buffer + 6, "%ld %ld", &size1, &size2); // Extract size1 and size2
            if(size1 < size2) {
                packFilesBySize(client_sock_fd, size1, size2);
            } else {
                write(client_sock_fd, "Error: size1 must be less than size2.\n", 38);
            }
        } else {
            if (write(client_sock_fd, "Unsupported operation\n", 23) < 0) error("ERROR writing to socket");
        }
    }
    close(client_sock_fd);  // Close client socket when done
}

void signalHandler(int signum) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

int main(void) {
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    signal(SIGCHLD, signalHandler); // To avoid zombie processes

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT_NO);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) error("ERROR on binding");

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    while (1) { // Main loop to accept connections
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) error("ERROR on accept");

        pid_t pid = fork();
        if (pid < 0) error("ERROR on fork");

        if (pid == 0) { // Child process
            close(sockfd); // Close listening socket in child
            crequest(newsockfd); // Handle client request
            exit(0); // Exit child process when done
        } else {
            close(newsockfd); // Close connected socket in parent
        }
        write(newsockfd, "END", 3);
    }
    close(sockfd); // This line is actually never reached
    return 0;
}
//server
