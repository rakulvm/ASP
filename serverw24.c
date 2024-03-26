#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h> // Include this header to resolve the warning
#include <ctype.h>  // For tolower function in sorting comparison

#define BUFFER_SIZE 256
#define PORT_NO 2024
#define MAX_DIRS 512  // Maximum number of directories we will handle


void error(const char *msg) {
    perror(msg);
    exit(1);
}

int dir_cmp(const void *a, const void *b) {
    // Comparison function for qsort, comparing strings case-insensitively
    return strcasecmp(*(const char **)a, *(const char **)b);
}

void listDirectories(int client_sock) {
    char *homeDir = getenv("HOME");
    if (!homeDir) homeDir = ".";
    DIR *d = opendir(homeDir);
    struct dirent *dir;
    char *dirs[MAX_DIRS];  // Array to hold directory names
    int count = 0;  // Number of directories added to dirs
    char buffer[BUFFER_SIZE];

    if (d) {
        while ((dir = readdir(d)) != NULL && count < MAX_DIRS) {
            if (dir->d_type == DT_DIR) {  // Check if it's a directory
                dirs[count] = strdup(dir->d_name);  // Duplicate and store the name
                if (dirs[count] == NULL) {  // Check for allocation failure
                    error("Failed to allocate memory for directory name");
                }
                count++;
            }
        }
        closedir(d);

        // Now, sort the directory names
        qsort(dirs, count, sizeof(char *), dir_cmp);

        // Send each directory name
        for (int i = 0; i < count; i++) {
            snprintf(buffer, sizeof(buffer), "%s\n", dirs[i]);
            if (write(client_sock, buffer, strlen(buffer)) < 0) error("ERROR writing to socket");
            free(dirs[i]);  // Free the duplicated directory name
        }

    } else {
        // Could not open directory
        if (write(client_sock, "ERROR opening directory\n", 25) < 0) error("ERROR writing to socket");
    }

    // End of directory list signal
    if (write(client_sock, "\n", 1) < 0) error("ERROR writing to socket");
}

void crequest(int client_sock_fd) {
    char buffer[BUFFER_SIZE];
    while (1) {  // Infinite loop to handle client commands
        bzero(buffer, BUFFER_SIZE);
        if (read(client_sock_fd, buffer, BUFFER_SIZE - 1) < 0) error("ERROR reading from socket");

        if (strncmp(buffer, "quitc", 5) == 0) {
            printf("Client has requested to close the connection.\n");
            break;  // Exit loop and end child process
        }

        if (strncmp(buffer, "dirlist -a", 10) == 0) {
            listDirectories(client_sock_fd);
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

    bzero((char *) &serv_addr, sizeof(serv_addr));
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
