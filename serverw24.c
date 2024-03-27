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

static FILE* tarFile;

typedef struct {
    char *name;
    time_t mod_time;
} DirEntry;

// Global variable to store found file info
struct fileInfo {
    char path[BUFFER_SIZE];
    int found;
} fileInfo;

void error(const char *msg) {
    perror(msg);
    exit(1);
}

int dir_time_cmp(const void *a, const void *b) {
    const DirEntry* dirA = *(const DirEntry**)a;
    const DirEntry* dirB = *(const DirEntry**)b;
    return (dirA->mod_time > dirB->mod_time) - (dirA->mod_time < dirB->mod_time);
}

int dir_cmp(const void *a, const void *b) {
    const DirEntry* dirA = *(const DirEntry**)a;
    const DirEntry* dirB = *(const DirEntry**)b;
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

// nftw callback function
static int fileCallback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (typeflag == FTW_F && strcmp(fileInfo.path, fpath + ftwbuf->base) == 0) {
        strncpy(fileInfo.path, fpath, BUFFER_SIZE);  // Store the full path of the found file
        fileInfo.found = 1;  // Mark as found
        return 1;  // Stop walking the directory tree
    }
    return 0;  // Continue walking
}

void sendFileInfo(int client_sock_fd, char *filename) {
    strncpy(fileInfo.path, filename, BUFFER_SIZE); // Copy filename to global structure
    fileInfo.found = 0;  // Reset found flag

    char *homeDir = getenv("HOME");
    if (!homeDir) homeDir = ".";

    nftw(homeDir, fileCallback, 20, 0);  // Start the walk at the home directory

    if (fileInfo.found) {
        struct stat file_stat;
        if (stat(fileInfo.path, &file_stat) == 0) {
            char buffer[BUFFER_SIZE * 2];
            char timebuff[256];
            strftime(timebuff, sizeof(timebuff), "%Y-%m-%d %H:%M:%S", localtime(&file_stat.st_mtime));
            snprintf(buffer, sizeof(buffer), "Filename: %s\nSize: %ld bytes\nDate modified: %s\nPermissions: %o\n",
                     fileInfo.path, file_stat.st_size, timebuff, file_stat.st_mode & 07777);
            write(client_sock_fd, buffer, strlen(buffer));
        } else {
            // If stat fails after finding the file
            write(client_sock_fd, "Error retrieving file info\n", 27);
        }
    } else {
        // If the file wasn't found
        write(client_sock_fd, "File not found\n", 16);
    }

    // End signal to indicate message completion
    write(client_sock_fd, "END\n", 4);
}

// Callback function for nftw
static int addToFileList(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf) {
    // Only consider regular files
    if (typeflag == FTW_F) {
        long size = (long)sb->st_size;
        long size1 = 1000; // Example size range, replace with your variables
        long size2 = 5000;

        if (size >= size1 && size <= size2) {
            // Append file path to tar command
            fprintf(tarFile, "%s\n", fpath);
        }
    }
    return 0; // Continue walking the tree
}

// Function to create tar.gz for files within size range
int createTarGzForSizeRange(long size1, long size2, const char* outputPath) {
    char fileListPath[] = "/tmp/filelist.txt";
    tarFile = fopen(fileListPath, "w");
    if (!tarFile) {
        perror("Failed to open file list");
        return -1;
    }

    // Walk the file tree starting from home directory
    char* homeDir = getenv("HOME");
    nftw(homeDir, addToFileList, 20, 0);

    fclose(tarFile);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "tar -czf %s -T %s", outputPath, fileListPath);
    system(cmd); // Execute tar command

    unlink(fileListPath); // Clean up file list

    return 0;
}

void sendFile(int sock_fd, const char* filePath) {
    int file_fd = open(filePath, O_RDONLY);
    if (file_fd < 0) {
        perror("Failed to open file");
        return;
    }

    char buffer[1024];
    ssize_t bytesRead;
    while ((bytesRead = read(file_fd, buffer, sizeof(buffer))) > 0) {
        if (write(sock_fd, buffer, bytesRead) < 0) {
            perror("Failed to send file");
            break;
        }
    }
    close(file_fd);
    
    // Indicate the end of the file transfer
    write(sock_fd, "END\n", 4);
}

void handleW24fzCommand(int client_sock_fd, long size1, long size2) {
    // Implement the file search logic here and create a tar.gz file
    // For simplicity, let's assume you have a function that does this:
    int result = createTarGzForSizeRange(size1, size2, "temp.tar.gz");
    
    if (result == 0) { // Assume 0 means success
        // Send the tar.gz file
        sendFile(client_sock_fd, "temp.tar.gz");
    } else {
        // Send "No file found" message
        const char *msg = "No file found\n";
        write(client_sock_fd, msg, strlen(msg));
        write(client_sock_fd, "END\n", 4);
    }
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
            strcpy(filename, buffer + 6); // Get the filename from the command
            sendFileInfo(client_sock_fd, filename);
        }  else if (strncmp(buffer, "w24fz ", 6) == 0) {
            long size1, size2;
            if (sscanf(buffer + 6, "%ld %ld", &size1, &size2) == 2 && size1 <= size2) {
                handleW24fzCommand(client_sock_fd, size1, size2);
            } else {
                const char *msg = "Invalid size range\n";
                write(client_sock_fd, msg, strlen(msg));
                write(client_sock_fd, "END\n", 4);
            }
        } else {
            // Handle other commands similarly
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
