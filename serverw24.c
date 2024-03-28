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

void sendFileInfo(int client_sock_fd, char *filename) {
    strncpy(fileInfo.path, filename, BUFFER_SIZE); // Copy filename to global structure
    fileInfo.found = 0;  // Reset found flag

    char *homeDir = getenv("HOME");
    if (!homeDir) homeDir = ".";

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

/*void packFilesInRange(int client_sock_fd, long size1, long size2) {
    char *homeDir = getenv("HOME");
    if (!homeDir) homeDir = ".";

    char command[BUFFER_SIZE * 3];
snprintf(command, sizeof(command),
         "find %s -type f \\( -size +%ldc -a -size -%ldc \\) -print0 | tar --null -czvf temp.tar.gz --files-from=-",
         homeDir, size1 - 1, size2 + 1);    

    // Execute the command and check if the tar file was created
    int status = system(command);
    if (status == -1) {
        write(client_sock_fd, "Error executing command\n", 25);
        return;
    }

    // Check the size of the generated tar file
    struct stat tar_stat;
    if (stat("temp.tar.gz", &tar_stat) == 0) {
        if (tar_stat.st_size > 0) {
            char buffer[BUFFER_SIZE];
            snprintf(buffer, sizeof(buffer), "Tar file created successfully: %ld bytes\n", tar_stat.st_size);
            write(client_sock_fd, buffer, strlen(buffer));
        } else {
            write(client_sock_fd, "No file found\n", 14);
            remove("temp.tar.gz"); // Remove the empty tar file
        }
    } else {
        write(client_sock_fd, "Error checking tar file\n", 25);
    }
}*/

void packFilesInRange(int client_sock_fd, long size1, long size2) {
    char *homeDir = getenv("HOME");
    if (!homeDir) homeDir = ".";

    char w24projectDir[BUFFER_SIZE];
    snprintf(w24projectDir, sizeof(w24projectDir), "%s/w24project", homeDir);

    // Create w24project directory if it does not exist
    mkdir(w24projectDir, 0777);

    // Find files in the specified size range and copy them to the w24project directory
    char command[BUFFER_SIZE * 4];
    snprintf(command, sizeof(command),
             "find %s -type f \\( -size +%ldc -a -size -%ldc \\) -exec bash -c 'cp \"$1\" \"%s/$(basename \\\"$1\\\")\"' -- {} \\;",
             homeDir, size1 - 1, size2 + 1, w24projectDir);

    int status = system(command);
    if (status != 0) {
        write(client_sock_fd, "Error finding or copying files\n", 31);
        return;
    }

    // Tar the contents of w24project directory
    snprintf(command, sizeof(command),
             "tar -czvf %s/temp.tar.gz -C %s .",
             homeDir, w24projectDir);

    status = system(command);
    if (status != 0) {
        write(client_sock_fd, "Error creating tar file\n", 25);
        return;
    }

    // Check the size of the generated tar file
    char tarPath[BUFFER_SIZE];
    snprintf(tarPath, sizeof(tarPath), "%s/temp.tar.gz", homeDir);
    struct stat tar_stat;
    if (stat(tarPath, &tar_stat) == 0) {
        if (tar_stat.st_size > 0) {
            char buffer[BUFFER_SIZE];
            snprintf(buffer, sizeof(buffer), "Tar file created successfully: %ld bytes\n", tar_stat.st_size);
            write(client_sock_fd, buffer, strlen(buffer));
        } else {
            write(client_sock_fd, "No file found\n", 14);
            remove(tarPath); // Remove the empty tar file
        }
    } else {
        write(client_sock_fd, "Error checking tar file\n", 25);
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
        } else if (strncmp(buffer, "w24fz ", 6) == 0) { // Check if the command is w24fz
            long size1, size2;
            if (sscanf(buffer + 6, "%ld %ld", &size1, &size2) == 2) {
                if (size1 > 0 && size2 >= size1) {
                    packFilesInRange(client_sock_fd, size1, size2);
                } else {
                    write(client_sock_fd, "Invalid size range\n", 20);
                }
            } else {
                write(client_sock_fd, "Invalid command format\n", 24);
            }
        }
        else {
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
