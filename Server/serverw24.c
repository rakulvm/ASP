/*we have basically defined some structures, header files and macros which we'll be using later.*/

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <ctype.h>
#include <time.h>
#include <ftw.h>
#include <glob.h>
#include <utime.h>
#include <errno.h>

#define MIRROR1_PORT 8001
#define MIRROR2_PORT 9001
#define BUFFER_SIZE 256
#define PORT_NO 2024
#define MAX_DIRS 512

int connectionCount = 1; //this is to redirect the connection according to the rule for redirection

#define MAX_MATCHING_FILES 1000

struct server_status {
    int connection_count;
} server_stat = {0};


struct {
    char *files[MAX_MATCHING_FILES];
    int count;
} matchingFiles;

static FILE* tarFile;

typedef struct {
    char *name;
    time_t mod_time;
} DirEntry;


struct fileInfo {
    char path[BUFFER_SIZE * 10];
    long size1;
    long size2;
    int found;
} fileInfo;


void error(const char *msg) {
    perror(msg);
    exit(1);
}

/*this is to senddata to client from server for dirlist functions*/
void sendData(int client_sock_fd, const char* data) {
    if (write(client_sock_fd, data, strlen(data)) < 0) 
        perror("ERROR writing to socket");
}

/*alpha sort is function for sorting list of subdirectories alphabetically which we use for dirlist -a, her we use stcasecmp and sort the list accordingly*/
int alphaSort(const void* a, const void* b) {
    const char* dirA = *(const char**)a;
    const char* dirB = *(const char**)b;
    return strcasecmp(dirA, dirB);
}

/*this is to perform sorting based on creation time*/
int timeSort(const void *a, const void *b) {
    DirEntry *dirA = (DirEntry *)a;
    DirEntry *dirB = (DirEntry *)b;
    return (dirA->mod_time > dirB->mod_time) - (dirA->mod_time < dirB->mod_time);
}


/*this function is called during dirlist -t, and performs sorting according to the subdirectory's creation time, and sends the client with the list sorted along with the timestamp so that the user can verify the same.*/
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
        sendData(client_sock_fd, "Failed to open directory.\nEND");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            snprintf(path, sizeof(path), "%s/%s", homeDir, entry->d_name);
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

/*this function is called during dirlist -a, and performs sorting according to the subdirectory's alphabetical order, and sends the client with the list sorted.*/
void listDirectoriesAlphabetically(int client_sock_fd) {
    DIR* dir;
    struct dirent* entry;
    char* directories[MAX_DIRS];
    int dirCount = 0;
    char buffer[BUFFER_SIZE];

    if ((dir = opendir(getenv("HOME"))) == NULL) {
        sendData(client_sock_fd, "Failed to open directory.\nEND");
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
        free(directories[i]);
    }

    sendData(client_sock_fd, "END\n");
}

/*create w24Project directory if it doesn't exist in the client's system*/
int createDirectory(const char *dir) {
    struct stat st = {0};

    if (stat(dir, &st) == -1) {
        if (mkdir(dir, 0777) != 0) {
            return -1;
        }
    }
    return 0;
}

/*this is to create tar file if the temp.tar.gz file doesn't exist in the client's system*/
int createTarFile(const char *dir, const char *tarName) {
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "tar -czf %s/%s -C %s .", dir, tarName, dir);

    return system(command);
}

/*This function would be called in packFilesByExtension. the usage of this is to check various cases for w24ft, like if the arguments are same, or if there are no args or there are arguments more than 3 and returns it accordingly to the packFilesByExtension*/
int validateExtensions(const char *extensions, int *count) {
    char extCopy[BUFFER_SIZE];
    char *token;
    char *extensionsArray[3] = {NULL, NULL, NULL};

    strncpy(extCopy, extensions, sizeof(extCopy));
    extCopy[sizeof(extCopy) - 1] = '\0';

    *count = 0;
    token = strtok(extCopy, " ");
    while (token && *count < 3) {
        for (int i = 0; i < *count; i++) {
            if (strcmp(extensionsArray[i], token) == 0) {
                return -1;
            }
        }
        extensionsArray[*count] = token;
        (*count)++;
        token = strtok(NULL, " ");
    }
    if (token) {
        return -2;
    }
    if (*count == 0) {
        return -3;
    }
    return 0;
}

/*This we use to pack the files into temp.tar.gz according to the extension given by user. It gets validation result from validateExtensions function and makes use of it to print error cases, and creates w24project if not there and tar if not there and then finds the correspondant file and then creates a tar and packs it up.*/
void packFilesByExtension(int client_sock_fd, const char *extensions) {
    char w24projectDir[BUFFER_SIZE];
    char tarFilePath[BUFFER_SIZE];
    char command[BUFFER_SIZE * 6];
    char extensionsCopy[BUFFER_SIZE];
    int extCount;
    int validationResult = validateExtensions(extensions, &extCount);
    char notification[BUFFER_SIZE];

    switch (validationResult) {
        case -1:
            snprintf(notification, sizeof(notification), "Error: Duplicate file types provided.\nEND");
            write(client_sock_fd, notification, strlen(notification));
            return;
        case -2:
            snprintf(notification, sizeof(notification), "Error: Number of extensions greater than the limit.\nEND");
            write(client_sock_fd, notification, strlen(notification));
            return;
        case -3:
            snprintf(notification, sizeof(notification), "Error: No file extensions provided.\nEND");
            write(client_sock_fd, notification, strlen(notification));
            return;
    }

    strncpy(extensionsCopy, extensions, sizeof(extensionsCopy));
    extensionsCopy[sizeof(extensionsCopy) - 1] = '\0';

    snprintf(w24projectDir, sizeof(w24projectDir), "%s/w24project", getenv("HOME") ? getenv("HOME") : ".");
    snprintf(tarFilePath, sizeof(tarFilePath), "%s/temp.tar.gz", w24projectDir);

    if (createDirectory(w24projectDir) != 0) {
        write(client_sock_fd, "Failed to create project directory.\n", 35);
        return;
    }

    unlink(tarFilePath);

    strcpy(command, "find ~ -type f \\( ");

    char *token = strtok(extensionsCopy, " ");
    while (token) {
        strcat(command, "-name '*.");
        strcat(command, token);
        strcat(command, "' ");
        token = strtok(NULL, " ");
        if (token) {
            strcat(command, "-o ");
        }
    }

    strcat(command, "\\) -print0 | tar --null -T - --transform 's/.*\\///' -czvf ");
    strcat(command, tarFilePath);

    int result = system(command);
    if (result != 0) {
        write(client_sock_fd, "Please check the tar file as the command takes time!.\nEND", 58);
        return;
    }

    struct stat tarStat;
    if (stat(tarFilePath, &tarStat) == -1 || tarStat.st_size == 0) {
        write(client_sock_fd, "No matching files found to pack.\n", 32);
        return;
    }

    snprintf(notification, sizeof(notification), "Files packed into %s\nEND", tarFilePath);
    write(client_sock_fd, notification, strlen(notification));
}

/*This is a helper function for sendFileInfo which we use to get the information about the file using nftw.*/
static int file_info(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (typeflag == FTW_F) {
        if (strcmp(fpath + ftwbuf->base, fileInfo.path) == 0) {
            fileInfo.found = 1;
            snprintf(fileInfo.path, sizeof(fileInfo.path), "%s", fpath);
            return 1;
        }
    }
    return 0;
}

/*this is a function which we use while user gives w24fn to get to know about the information of file. It gets information from helper function, performs some validation if file not there before calling helper function likewise and sends info to client as well*/
void sendFileInfo(int client_sock_fd, char *filename) {
    char buffer[BUFFER_SIZE * 2];
    char timebuff[256];
    struct stat file_stat;

    fileInfo.found = 0;
    strncpy(fileInfo.path, filename, BUFFER_SIZE);

    nftw(getenv("HOME"), file_info, 20, FTW_PHYS);

    if (fileInfo.found) {
        if (stat(fileInfo.path, &file_stat) == 0) {
            strftime(timebuff, sizeof(timebuff), "%Y-%m-%d %H:%M:%S", localtime(&file_stat.st_mtime));
            snprintf(buffer, sizeof(buffer), "Filename: %s\nSize: %ld bytes\nDate modified: %s\nPermissions: %o\n",
                     fileInfo.path, file_stat.st_size, timebuff, file_stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
            write(client_sock_fd, buffer, strlen(buffer));
        } else {
            snprintf(buffer, sizeof(buffer), "Error retrieving file info\nEND");
            write(client_sock_fd, buffer, strlen(buffer));
        }
    } else {
        snprintf(buffer, sizeof(buffer), "File not found\nEND");
        write(client_sock_fd, buffer, strlen(buffer));
    }

    snprintf(buffer, sizeof(buffer), "END\n");
    write(client_sock_fd, buffer, strlen(buffer));
}

/*So this function basically is called when user gives w24fz range1 range2, so it does the same basical tar creation along with w24project and tar check, and find the file within the given range using the system command and packs only the files into the tar within the given range for the home directory*/
void packFilesBySize(int client_sock_fd, long size1, long size2) {
    char w24projectDir[BUFFER_SIZE];
    char fileListPath[BUFFER_SIZE];
    char tarFilePath[BUFFER_SIZE];
    FILE *fp;
    int status;

    snprintf(w24projectDir, sizeof(w24projectDir), "%s/w24project", getenv("HOME") ? getenv("HOME") : ".");
    snprintf(fileListPath, sizeof(fileListPath), "%s/filelist.txt", w24projectDir);
    snprintf(tarFilePath, sizeof(tarFilePath), "%s/temp.tar.gz", w24projectDir);

    if (createDirectory(w24projectDir) != 0) {
        write(client_sock_fd, "Failed to create project directory.\n", 35);
        return;
    }

    unlink(tarFilePath);
    unlink(fileListPath);

    char findCommand[BUFFER_SIZE * 3];
    snprintf(findCommand, sizeof(findCommand),
             "find ~ -type f \\( -size +%ldc -a -size -%ldc \\) -print > %s",
             size1, size2, fileListPath);

    status = system(findCommand);
    if (status != 0) {
        write(client_sock_fd, "Failed to find files or no files found.\n", 38);
        return;
    }

    char tarCommand[BUFFER_SIZE * 3];
    snprintf(tarCommand, sizeof(tarCommand),
             "tar --transform 's#.*/##' -czf %s -T %s",
             tarFilePath, fileListPath);

    status = system(tarCommand);
    if (status != 0) {
        write(client_sock_fd, "Please check the tar file as the command takes time!.\nEND", 58);
        return;
    }

    char notification[BUFFER_SIZE];
    snprintf(notification, sizeof(notification), "Files packed into %s\nEND", tarFilePath);
    write(client_sock_fd, notification, strlen(notification));
}

int isFileOlderThan(struct stat *file_stat, struct tm *given_date) {
    struct tm *file_date = gmtime(&(file_stat->st_mtime));
    return difftime(mktime(file_date), mktime(given_date)) <= 0;
}

/*this function packs files into tar when user gives w24fdb date, so this what it does is it checks if the file has been created before or on the given date and retrives all such into a tar and prints and message to client and sends the tar file as well. It performs the validations, w24Project , tar file creations if not there as well.*/
void packFilesByDate(int client_sock_fd, const char *date) {
    char w24projectDir[BUFFER_SIZE];
    char fileListPath[BUFFER_SIZE];
    char tarFilePath[BUFFER_SIZE];
    FILE *fp;
    int status;

    snprintf(w24projectDir, sizeof(w24projectDir), "%s/w24project", getenv("HOME") ? getenv("HOME") : ".");
    snprintf(fileListPath, sizeof(fileListPath), "%s/filelist.txt", w24projectDir);
    snprintf(tarFilePath, sizeof(tarFilePath), "%s/temp.tar.gz", w24projectDir);

    if (createDirectory(w24projectDir) != 0) {
        write(client_sock_fd, "Failed to create project directory.\n", 35);
        return;
    }

    unlink(tarFilePath);
    unlink(fileListPath);

    struct tm given_date;
    memset(&given_date, 0, sizeof(struct tm));
    strptime(date, "%Y-%m-%d", &given_date);
    time_t given_time = mktime(&given_date);

    char findCommand[BUFFER_SIZE * 3];
    snprintf(findCommand, sizeof(findCommand), "find ~ -type f -printf '%%T@ %%p\\n'");
    fp = popen(findCommand, "r");
    if (fp == NULL) {
        write(client_sock_fd, "Failed to execute find command.\n", 31);
        return;
    }

    FILE *fileList = fopen(fileListPath, "w");
    if (fileList == NULL) {
        pclose(fp);
        write(client_sock_fd, "Failed to create file list.\n", 28);
        return;
    }

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

    char tarCommand[BUFFER_SIZE * 3];
    snprintf(tarCommand, sizeof(tarCommand),
             "tar --transform 's#.*/##' -czf %s -T %s",
             tarFilePath, fileListPath);

    status = system(tarCommand);
    if (status != 0) {
        write(client_sock_fd, "Please check the tar file as the command takes time!.\nEND", 58);
        return;
    }

    char notification[BUFFER_SIZE];
    snprintf(notification, sizeof(notification), "Files packed into %s\nEND", tarFilePath);
    write(client_sock_fd, notification, strlen(notification));
}

/*this function packs files into tar when user gives w24fdb date, so this what it does is it checks if the file has been created after or on the given date and retrives all such into a tar and prints and message to client and sends the tar file as well. It performs the validations, w24Project , tar file creations if not there as well.*/
void packFilesByDateGreat(int client_sock_fd, const char *date) {
    char w24projectDir[BUFFER_SIZE];
    char fileListPath[BUFFER_SIZE];
    char tarFilePath[BUFFER_SIZE];
    FILE *fp;
    int status;

    snprintf(w24projectDir, sizeof(w24projectDir), "%s/w24project", getenv("HOME") ? getenv("HOME") : ".");
    snprintf(fileListPath, sizeof(fileListPath), "%s/filelist.txt", w24projectDir);
    snprintf(tarFilePath, sizeof(tarFilePath), "%s/temp.tar.gz", w24projectDir);

    if (createDirectory(w24projectDir) != 0) {
        write(client_sock_fd, "Failed to create project directory.\n", 35);
        return;
    }

    unlink(tarFilePath);
    unlink(fileListPath);

    struct tm given_date;
    memset(&given_date, 0, sizeof(struct tm));
    strptime(date, "%Y-%m-%d", &given_date);
    time_t given_time = mktime(&given_date);

    time_t current_time = time(NULL);
    if (difftime(given_time, current_time) > 0) {
        write(client_sock_fd, "Error: Date is in the future.\nEND", 33);
        return;
    }

    char findCommand[BUFFER_SIZE * 3];
    snprintf(findCommand, sizeof(findCommand), "find ~ -type f -printf '%%T@ %%p\\n'");
    fp = popen(findCommand, "r");
    if (fp == NULL) {
        write(client_sock_fd, "Failed to execute find command.\n", 31);
        return;
    }

    FILE *fileList = fopen(fileListPath, "w");
    if (fileList == NULL) {
        pclose(fp);
        write(client_sock_fd, "Failed to create file list.\n", 28);
        return;
    }

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

    struct stat fileStat;
    if (stat(fileListPath, &fileStat) == 0 && fileStat.st_size == 0) {
        unlink(tarFilePath);
        unlink(fileListPath);
        write(client_sock_fd, "No files found for the specified date.\n", 39);
        return;
    }

    char tarCommand[BUFFER_SIZE * 3];
    snprintf(tarCommand, sizeof(tarCommand),
             "tar --transform 's#.*/##' -czf %s -T %s",
             tarFilePath, fileListPath);

    status = system(tarCommand);
    if (status != 0) {
        write(client_sock_fd, "Please check the tar file as the command takes time!.\nEND", 58);
        return;
    }

    char notification[BUFFER_SIZE];
    snprintf(notification, sizeof(notification), "Files packed into %s\nEND", tarFilePath);
    write(client_sock_fd, notification, strlen(notification));
}

/*this is a key function which handles various inputs given from client and does comparisions and calls the suitable functions and does writes into client, and it also performs some validations as well*/
void crequest(int client_sock_fd) {
    char buffer[BUFFER_SIZE];
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t n = read(client_sock_fd, buffer, BUFFER_SIZE - 1);
        if (n < 0) {
            perror("ERROR reading from socket");
            break;
        }

        if (strncmp(buffer, "quitc", 5) == 0) {
            printf("A Client has closed the connection.\n");
            break;
        }

        if (strncmp(buffer, "dirlist -a", 10) == 0) {
            listDirectoriesAlphabetically(client_sock_fd);
        } else if (strncmp(buffer, "dirlist -t", 10) == 0) {
            listDirectoriesByCreationTime(client_sock_fd);
        } else if (strncmp(buffer, "w24fn ", 6) == 0) {
            char filename[BUFFER_SIZE];
            strncpy(filename, buffer + 6, BUFFER_SIZE);
            sendFileInfo(client_sock_fd, filename);
        } else if (strncmp(buffer, "w24ft ", 6) == 0) {
            char extensions[BUFFER_SIZE];
            strcpy(extensions, buffer + 6);
            packFilesByExtension(client_sock_fd, extensions);
        } else if (strncmp(buffer, "w24fz ", 6) == 0) {
            long size1, size2;
            int numArgs = sscanf(buffer + 6, "%ld %ld", &size1, &size2);
            if (numArgs == 2 && size1 < size2) {
                packFilesBySize(client_sock_fd, size1, size2);
            } else {
                const char *errorMsg = "Error: Please check the no of args (or) size1 is greater than size2 (or) enter only numbers!.\nEND";
                write(client_sock_fd, errorMsg, strlen(errorMsg));
            }
        } else if (strncmp(buffer, "w24fdb ", 7) == 0) {
            char dateStr[BUFFER_SIZE];
            char extraArgCheck[BUFFER_SIZE];
            int numArgs = sscanf(buffer + 7, "%s %s", dateStr, extraArgCheck);

            if (numArgs == 1) {
                struct tm date;
                memset(&date, 0, sizeof(struct tm));
                char *endPtr = strptime(dateStr, "%Y-%m-%d", &date);

                if (endPtr != NULL && *endPtr == '\0') {
                    packFilesByDate(client_sock_fd, dateStr);
                } else {
                    write(client_sock_fd, "Error: Invalid date format. Use YYYY-MM-DD.\nEND", 48);
                }
            } else if (numArgs > 1) {
                write(client_sock_fd, "Error: Too many arguments for w24fdb command.\nEND", 70);
            } else {
                write(client_sock_fd, "Error: Incorrect format for w24fdb command. Use: w24fdb YYYY-MM-DD\nEND", 70);
            }

        } else if (strncmp(buffer, "w24fda ", 7) == 0) {
            char dateStr[BUFFER_SIZE];
            char extraArgCheck[BUFFER_SIZE];
            int numArgs = sscanf(buffer + 7, "%s %s", dateStr, extraArgCheck);

            if (numArgs == 1) {
                struct tm date;
                memset(&date, 0, sizeof(struct tm));
                char *endPtr = strptime(dateStr, "%Y-%m-%d", &date);

                if (endPtr != NULL && *endPtr == '\0') {
                    packFilesByDateGreat(client_sock_fd, dateStr);
                } else {
                    write(client_sock_fd, "Error: Invalid date format. Use YYYY-MM-DD.\nEND", 48);
                }
            } else if (numArgs > 1) {
                write(client_sock_fd, "Error: Too many arguments for w24fdb command.\nEND", 70);
            } else {
                write(client_sock_fd, "Error: Incorrect format for w24fdb command. Use: w24fdb YYYY-MM-DD\nEND", 70);
            }
        } else {
            if (write(client_sock_fd, "Unsupported operation\nEND", 26) < 0) error("ERROR writing to socket");
        }
    }
    close(client_sock_fd);
}

/*this is for emergency purpose, to avoid zombie process*/
void signalHandler(int signum) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

/*the main function acts as a server for socket programming and it does redirections according to the rules given in the assignment and calls the crequest function only on the success of child process, because for every client request a child process must be created.*/
int main(void) {
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    char redirectMessage[BUFFER_SIZE];

    signal(SIGCHLD, signalHandler);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket\nEND");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT_NO);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) error("ERROR on binding\nEND");

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    while (1) {
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("ERROR on accept");
                continue;
            }
        }

        connectionCount++;

        if ((connectionCount > 3 && connectionCount <= 6)) {
            snprintf(redirectMessage, BUFFER_SIZE, "redirect %d\n", MIRROR1_PORT);
            write(newsockfd, redirectMessage, strlen(redirectMessage));
            close(newsockfd);
            continue;
        } else if (connectionCount > 6 && connectionCount <= 9) {
            snprintf(redirectMessage, BUFFER_SIZE, "redirect %d\n", MIRROR2_PORT);
            write(newsockfd, redirectMessage, strlen(redirectMessage));
            close(newsockfd);
            continue;
        } else if (connectionCount >= 10) {
            int port;
            int roundRobin = (connectionCount - 10) % 3;
            if (roundRobin == 0) {
                port = PORT_NO;
            } else if (roundRobin == 1) {
                port = MIRROR1_PORT;
            } else {
                port = MIRROR2_PORT;
            }
            snprintf(redirectMessage, BUFFER_SIZE, "redirect %d\n", port);
            write(newsockfd, redirectMessage, strlen(redirectMessage));
            close(newsockfd);
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("ERROR on fork");
        } else if (pid == 0) {
            close(sockfd);
            crequest(newsockfd);
            exit(0);
        } else {
            close(newsockfd);
        }
    }
    close(sockfd);
    return 0;
}

