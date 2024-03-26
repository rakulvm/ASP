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

#define BUFFER_SIZE 256
#define PORT_NO 2024

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void listDirectories(int client_sock) {
    DIR *d;
    struct dirent *dir;
    d = opendir(".");
    char buffer[1024];
    int n;

    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_DIR) { // Check if it's a directory
                snprintf(buffer, sizeof(buffer), "%s\n", dir->d_name);
                n = write(client_sock, buffer, strlen(buffer));
                if (n < 0) error("ERROR writing to socket");
            }
        }
        closedir(d);
    } else {
        // Could not open directory
        n = write(client_sock, "ERROR opening directory\n", 25);
        if (n < 0) error("ERROR writing to socket");
    }

    // Signal end of directory list
    snprintf(buffer, sizeof(buffer), "\n");
    n = write(client_sock, buffer, strlen(buffer));
    if (n < 0) error("ERROR writing to socket");
}


// Function to list files in the directory
void listFiles(int sock, char *dir, int alphabetical) {
    DIR *d;
    struct dirent *dir_entry;
    char response[BUFFER_SIZE];
    int res_size;
    d = opendir(dir);

    if (!d) {
        sprintf(response, "Failed to open directory: %s\n", dir);
        write(sock, response, strlen(response));
        return;
    }

    while ((dir_entry = readdir(d)) != NULL) {
        if (dir_entry->d_type == DT_REG) { // Check if it's a regular file
            res_size = snprintf(response, BUFFER_SIZE, "%s\n", dir_entry->d_name);
            write(sock, response, res_size);
        }
    }

    closedir(d);
}

void crequest(int client_sock_fd) {
    char buffer[BUFFER_SIZE];
    int read_status;

    while (1) {
        bzero(buffer, BUFFER_SIZE);
        read_status = read(client_sock_fd, buffer, BUFFER_SIZE - 1);
        if (read_status < 0) error("ERROR reading from socket");

        if (strncmp(buffer, "quitc", 5) == 0) {
            printf("Client has requested to close the connection.\n");
            break;
        }

        // Handle listing files
        if (strncmp(buffer, "listfiles", 9) == 0) {
            listFiles(client_sock_fd, ".", 1); // List files in current directory
        } else if (strncmp(buffer, "dirlist -a", 10) == 0) {
            listDirectories(client_sock_fd);
        } else {
            char *errmsg_unsupported = "Unsupported operation\n";
            write(client_sock_fd, errmsg_unsupported, strlen(errmsg_unsupported));
        }
    }

    close(client_sock_fd);
}

void signalHandler(int signum) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

int main(void) {
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    signal(SIGCHLD, signalHandler);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT_NO);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    while (1) {
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) error("ERROR on accept");

        pid_t pid = fork();
        if (pid < 0) error("ERROR on fork");    

        if (pid == 0) {
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

