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
#include <utime.h>

#define BUFFER_SIZE 256
#define PORT_NO 2024
#define MAX_DIRS 512
#ifndef DT_DIR
#define DT_DIR 4
#endif


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


// Error handling function
void error(const char *msg) {
    perror(msg);
    exit(1);
}

// Helper function to send data through the socket
void sendData(int client_sock_fd, const char* data) {
    if (write(client_sock_fd, data, strlen(data)) < 0) 
        perror("ERROR writing to socket");
}

// Comparator function for alphabetical sorting
int alphaSort(const void* a, const void* b) {
    const char* dirA = *(const char**)a;
    const char* dirB = *(const char**)b;
    return strcasecmp(dirA, dirB);
}

// Comparator for sorting directories by creation time
int timeSort(const void *a, const void *b) {
    DirEntry *dirA = (DirEntry *)a;
    DirEntry *dirB = (DirEntry *)b;
    return (dirA->mod_time > dirB->mod_time) - (dirA->mod_time < dirB->mod_time);
}

void listDirectoriesByCreationTime(int client_sock_fd) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    DirEntry directories[MAX_DIRS];
    int dirCount = 0;
    char buffer[BUFFER_SIZE * 2], path[BUFFER_SIZE];
    char timeBuff[64];

    char *homeDir = getenv("HOME");
    if ((dir = opendir(homeDir)) == NULL) {
        sendData(client_sock_fd, "Failed to open directory.\n");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            snprintf(path, sizeof(path), "%s/%s", homeDir, entry->d_name);  // Generate absolute path
            if (stat(path, &statbuf) == -1) {
                perror("stat");
                continue;
            }
            directories[dirCount].name = strdup(entry->d_name);
            directories[dirCount].mod_time = statbuf.st_mtime;
            dirCount++;
            if (dirCount >= MAX_DIRS) break;
        }
    }
    closedir(dir);

    qsort(directories, dirCount, sizeof(DirEntry), timeSort);

    for (int i = 0; i < dirCount; i++) {
        strftime(timeBuff, sizeof(timeBuff), "%Y-%m-%d %H:%M:%S", localtime(&directories[i].mod_time));
        snprintf(buffer, sizeof(buffer), "%-30s %s\n", timeBuff, directories[i].name);
        sendData(client_sock_fd, buffer);
        free(directories[i].name);
    }

    sendData(client_sock_fd, "END\n");
}
// Function to list directories alphabetically
void listDirectoriesAlphabetically(int client_sock_fd) {
    DIR* dir;
    struct dirent* entry;
    char* directories[MAX_DIRS];
    int dirCount = 0;
    char buffer[BUFFER_SIZE];

    if ((dir = opendir(getenv("HOME"))) == NULL) {
        sendData(client_sock_fd, "Failed to open directory.\n");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            directories[dirCount++] = strdup(entry->d_name);
            if (dirCount >= MAX_DIRS) break;
        }
    }
    closedir(dir);

    qsort(directories, dirCount, sizeof(char*), alphaSort);

    for (int i = 0; i < dirCount; i++) {
        snprintf(buffer, sizeof(buffer), "%s\n", directories[i]);
        sendData(client_sock_fd, buffer);
        free(directories[i]);  // Free the duplicated string
    }

    sendData(client_sock_fd, "END\n");
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

// Helper function to check if file's modification date is less than or equal to given date
int isFileOlderThan(struct stat *file_stat, struct tm *given_date) {
    struct tm *file_date = gmtime(&(file_stat->st_mtime)); // Convert to GMT for comparison
    return difftime(mktime(file_date), mktime(given_date)) <= 0;
}

// Function to handle the 'w24fdb <date>' command
void packFilesByDate(int client_sock_fd, const char *date) {
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

    // Parse the given date
    struct tm given_date;
    memset(&given_date, 0, sizeof(struct tm));
    strptime(date, "%Y-%m-%d", &given_date);
    time_t given_time = mktime(&given_date);

    // Execute the find command and write the file names to fileListPath
    char findCommand[BUFFER_SIZE * 3];
    snprintf(findCommand, sizeof(findCommand), "find ~ -type f -printf '%%T@ %%p\\n'");
    fp = popen(findCommand, "r");
    if (fp == NULL) {
        write(client_sock_fd, "Failed to execute find command.\n", 31);
        return;
    }

    // Open the file list to write
    FILE *fileList = fopen(fileListPath, "w");
    if (fileList == NULL) {
        pclose(fp);
        write(client_sock_fd, "Failed to create file list.\n", 28);
        return;
    }

    // Read the output from find, parse it, and write to fileList
    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), fp) != NULL) {
        double epoch_time;
        char file_path[BUFFER_SIZE];
        if (sscanf(line, "%lf %s", &epoch_time, file_path) == 2) {
            if (epoch_time <= (double)given_time) {
                fprintf(fileList, "%s\n", file_path);
            }
        }
    }
    fclose(fileList);
    pclose(fp);

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

// Function to handle the 'w24fdb <date>' command
void packFilesByDateGreat(int client_sock_fd, const char *date) {
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

    // Parse the given date
    struct tm given_date;
    memset(&given_date, 0, sizeof(struct tm));
    strptime(date, "%Y-%m-%d", &given_date);
    time_t given_time = mktime(&given_date);

    // Execute the find command and write the file names to fileListPath
    char findCommand[BUFFER_SIZE * 3];
    snprintf(findCommand, sizeof(findCommand), "find ~ -type f -printf '%%T@ %%p\\n'");
    fp = popen(findCommand, "r");
    if (fp == NULL) {
        write(client_sock_fd, "Failed to execute find command.\n", 31);
        return;
    }

    // Open the file list to write
    FILE *fileList = fopen(fileListPath, "w");
    if (fileList == NULL) {
        pclose(fp);
        write(client_sock_fd, "Failed to create file list.\n", 28);
        return;
    }

    // Read the output from find, parse it, and write to fileList
    char line[BUFFER_SIZE];
    while (fgets(line, sizeof(line), fp) != NULL) {
        double epoch_time;
        char file_path[BUFFER_SIZE];
        if (sscanf(line, "%lf %s", &epoch_time, file_path) == 2) {
            if (epoch_time >= (double)given_time) {
                fprintf(fileList, "%s\n", file_path);
            }
        }
    }
    fclose(fileList);
    pclose(fp);

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
        	listDirectoriesAlphabetically(client_sock_fd);
	} else if (strncmp(buffer, "dirlist -t", 10) == 0) {
		listDirectoriesByCreationTime(client_sock_fd);
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
        } else if (strncmp(buffer, "w24fdb ", 7) == 0) {
            // Extract the date string from the command
            char dateStr[BUFFER_SIZE];
            strncpy(dateStr, buffer + 7, BUFFER_SIZE - 7);

            // Validate the date format (YYYY-MM-DD)
            struct tm date;
            if (strptime(dateStr, "%Y-%m-%d", &date) == NULL) {
                write(client_sock_fd, "Invalid date format.\n", 20);
            } else {
                // Call the function to pack files by date
                packFilesByDate(client_sock_fd, dateStr);
            }
        } else if (strncmp(buffer, "w24fda ", 7) == 0) {
            // Extract the date string from the command
            char dateStr[BUFFER_SIZE];
            strncpy(dateStr, buffer + 7, BUFFER_SIZE - 7);

            // Validate the date format (YYYY-MM-DD)
            struct tm date;
            if (strptime(dateStr, "%Y-%m-%d", &date) == NULL) {
                write(client_sock_fd, "Invalid date format.\n", 20);
            } else {
                // Call the function to pack files by date
                packFilesByDateGreat(client_sock_fd, dateStr);
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
