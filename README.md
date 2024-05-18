# Client-Server File Request System

## Overview

This project is a client-server file request system implemented in C. It allows multiple clients to connect to a server to request files or directories. The server can handle multiple requests simultaneously, distributing the load across a primary server and two mirror servers.

## Features

- **Multi-client Support**: Multiple clients can connect to the server and request files or directories.
- **File and Directory Listing**: Clients can request lists of subdirectories sorted either alphabetically or by creation date.
- **File Details Retrieval**: Clients can request specific file details, including size, creation date, and permissions.
- **File Archive Retrieval**: Clients can request files within a specific size range, files of specific types, or files created before or after a certain date.
- **Load Balancing**: The server and two mirror servers handle client requests in an alternating manner to balance the load.
- **Socket Communication**: Communication between clients and servers is handled using sockets.

## Commands

Clients can use the following commands to interact with the server:

- `dirlist -a`: Lists subdirectories in alphabetical order.
- `dirlist -t`: Lists subdirectories by creation date.
- `w24fn <filename>`: Retrieves details of a specified file.
- `w24fz <size1> <size2>`: Retrieves files within the specified size range.
- `w24ft <extension1> [<extension2> <extension3>]`: Retrieves files of the specified types.
- `w24fdb <date>`: Retrieves files created before the specified date.
- `w24fda <date>`: Retrieves files created after the specified date.
- `quitc`: Terminates the client process.

## How It Works

1. **Server Setup**: The main server (`serverw24`) and two mirror servers (`mirror1` and `mirror2`) are initialized and run on separate machines.
2. **Client Connection**: Clients connect to the server and send commands.
3. **Command Processing**: The server forks a child process for each client request, processes the command, and returns the result to the client.
4. **Load Distribution**: Client connections are distributed across the server and mirror servers in a round-robin fashion.

## File Structure

- `serverw24.c`: Main server implementation.
- `clientw24.c`: Client implementation.
- `mirror1.c`: First mirror server implementation.
- `mirror2.c`: Second mirror server implementation.

## Setup and Compilation

1. Compile the server and client programs:
   ```sh
   gcc -o serverw24 serverw24.c
   gcc -o clientw24 clientw24.c
   gcc -o mirror1 mirror1.c
   gcc -o mirror2 mirror2.c

2. Run the server and mirrors on different terminals/machines:
   ```sh
   Copy code
   ./serverw24
   ./mirror1
   ./mirror2

3. Run the client on another terminal/machine:
   ```sh
   Copy code
   ./clientw24
