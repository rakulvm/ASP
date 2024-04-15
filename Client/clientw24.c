#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define DEFAULT_PORT 2024

void error(const char *msg) {
    perror(msg);
    exit(1);
}

int isValidCommand(const char *cmd) {
    const char *validCommands[] = {"dirlist -a", "quitc", "dirlist -t", NULL};

    for (int i = 0; validCommands[i] != NULL; i++) {
        if (strcmp(cmd, validCommands[i]) == 0) {
            return 1;
        }
    }

    if (strncmp(cmd, "w24fn ", 6) == 0 || strncmp(cmd, "w24ft ", 6) == 0 || 
        strncmp(cmd, "w24fz ", 6) == 0 || strncmp(cmd, "w24fdb ", 6) == 0 || 
        strncmp(cmd, "w24fda ", 6) == 0) {
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int sockfd, portno = DEFAULT_PORT;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[BUFFER_SIZE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    server = gethostbyname("localhost");
    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);

    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
        error("ERROR connecting");

    // Declare a variable to store the last command
    char lastCommand[BUFFER_SIZE] = {0};
    while (1) {
        printf("$clientw24: ");
        bzero(buffer, BUFFER_SIZE);
        fgets(buffer, BUFFER_SIZE - 1, stdin);
        buffer[strcspn(buffer, "\n")] = '\0'; // remove newline
	
	// Check for the quit command
        if (strncmp(buffer, "quitc", 5) == 0) {
	    printf("Quitting client.\n");
	    if (write(sockfd, "quitc", 5) < 0) // Send quitc command to the server
		error("ERROR writing quit to socket");
	    close(sockfd); // Close the socket immediately
	    exit(0); // Exit the client program
	}
	
        if (!isValidCommand(buffer)) {
            printf("Invalid command.\n");
            continue;
        }
        
        strncpy(lastCommand, buffer, BUFFER_SIZE);

        // Write the command to the server
        if (write(sockfd, buffer, strlen(buffer)) < 0) 
            error("ERROR writing to socket");

        do {
        // Read the server's response
        bzero(buffer, BUFFER_SIZE);
        ssize_t n = read(sockfd, buffer, BUFFER_SIZE - 1);
	
	if (n < 0) {
		error("ERROR reading from socket");
        } else if (n == 0) {
		// No response from server, possibly the server closed the connection
		printf("Server closed the connection.\n");
		close(sockfd);
		exit(1);
        }
        // buffer[n] = '\0';

        // Check for redirect response
        if (strncmp(buffer, "redirect", 8) == 0) {
            int new_port;
            sscanf(buffer, "redirect %d", &new_port);
            printf("Redirecting to port %d\n", new_port);
            close(sockfd);

            // Create a new socket and connect to the new port
            sockfd = socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd < 0) error("ERROR opening socket");

            serv_addr.sin_port = htons(new_port);
            if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
                error("ERROR connecting");
	    
	    // After connecting to the new port, resend the last command
            if (write(sockfd, lastCommand, strlen(lastCommand)) < 0) {
                error("ERROR writing to socket after redirect");
            }
	    
            continue;  
        }
        
        buffer[n] = '\0'; 

        printf("%s\n", buffer); // Print the response
        
        } while (strstr(buffer, "END") == NULL);
        
        /*if (strncmp(lastCommand, "quitc", 5) != 0) {
    	printf("$clientw24: ");
	}*/

    }

    close(sockfd); // Close the socket
    return 0;
}
