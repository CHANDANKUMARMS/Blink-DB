
// ----- Begin main.cpp -----
#include "blinkdb.h"
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <iostream>
#include <stdexcept>


#define MAX_EVENTS 10       ///< Maximum number of events to process per epoll_wait call.
#define BUFFER_SIZE 4096    ///< Buffer size for reading client data.
#define PORT 9001           ///< Port number for the server to listen on.

/**
 * @brief A simple RESP (REdis Serialization Protocol) parser and serializer.
 *
 * This class provides static methods to parse RESP input and serialize responses.
 */
class RESPParser {
public:
    /**
     * @brief Parses a RESP-formatted input string into tokens.
     *
     * The function expects the input to be in RESP format and extracts tokens accordingly.
     *
     * @param input The RESP-formatted string.
     * @return A vector of tokens extracted from the input.
     */
    static std::vector<std::string> parse(const std::string &input) {
        std::vector<std::string> tokens;
        if (input.empty() || input.front() != '*')
            return tokens;
        size_t pos = 1;
        int totalTokens = std::stoi(input.substr(pos, input.find("\r\n", pos) - pos));
        pos = input.find("\r\n", pos) + 2;
        for (int i = 0; i < totalTokens; ++i) {
            if (pos >= input.size()) break;
            if (input[pos] == '$') {
                ++pos;
                int tokenLength = std::stoi(input.substr(pos, input.find("\r\n", pos) - pos));
                pos = input.find("\r\n", pos) + 2;
                tokens.push_back(input.substr(pos, tokenLength));
                pos += tokenLength + 2;
            }
        }
        return tokens;
    }

    /**
     * @brief Serializes data into RESP format.
     *
     * For "NULL" data, a special RESP representation is returned.
     *
     * @param data The data to serialize.
     * @return A RESP-formatted string representing the data.
     */
    static std::string serialize(const std::string &data) {
        if (data == "NULL")
            return "$-1\r\n";
        return "$" + std::to_string(data.size()) + "\r\n" + data + "\r\n";
    }

    /**
     * @brief Generates a RESP "OK" message.
     *
     * @return A RESP-formatted "OK" message.
     */
    static std::string ok() { return "+OK\r\n"; }

    /**
     * @brief Generates a RESP error message.
     *
     * @param msg The error message.
     * @return A RESP-formatted error message.
     */
    static std::string error(const std::string &msg) { return "-ERR " + msg + "\r\n"; }
};

/**
 * @brief Handles an individual client request.
 *
 * Reads the incoming data, parses the RESP command, executes it against BlinkDB,
 * and sends back the appropriate response.
 *
 * @param clientFd The file descriptor for the client connection.
 * @param db A reference to the BlinkDB instance.
 */
void handleClientRequest(int clientFd, BlinkDB &db) {
    char buffer[BUFFER_SIZE];
    ssize_t readBytes = read(clientFd, buffer, BUFFER_SIZE);
    if (readBytes <= 0) {
        close(clientFd);
        return;
    }
    std::string request(buffer, readBytes);
    std::vector<std::string> args = RESPParser::parse(request);
    std::string response;
    try {
        if (args.empty()) {
            response = RESPParser::error("Empty command");
        } else if (args[0] == "PING") {
            response = "+PONG\r\n";
        } else if (args[0] == "SET" && args.size() >= 3) {
            db.set(args[1], args[2]);
            response = RESPParser::ok();
        } else if (args[0] == "GET" && args.size() >= 2) {
            std::string value = db.get(args[1]);
            response = (value == "NULL") ? RESPParser::serialize("") : RESPParser::serialize(value);
        } else if (args[0] == "DEL" && args.size() >= 2) {
            bool result = db.del(args[1]);
            response = result ? ":1\r\n" : ":0\r\n";
        } else {
            response = RESPParser::error("Unsupported command");
        }
    } catch (...) {
        response = RESPParser::error("Command processing failed");
    }
    send(clientFd, response.c_str(), response.size(), 0);
}

/**
 * @brief Entry point for the BlinkDB server application.
 *
 * Sets up a TCP server, initializes epoll for I/O multiplexing, and handles client connections.
 *
 * @return int Exit status.
 */
int main() {
    BlinkDB database;
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        perror("socket creation error");
        return 1;
    }
    
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind failed");
        return 1;
    }
    
    if (listen(serverSocket, SOMAXCONN) < 0) {
        perror("listen error");
        return 1;
    }

    std::cout << "Server is running on port " << PORT << std::endl;




    int epollInstance = epoll_create1(0);
    if (epollInstance < 0) {
        perror("epoll_create1 error");
        return 1;
    }
    
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = serverSocket;
    if (epoll_ctl(epollInstance, EPOLL_CTL_ADD, serverSocket, &event) < 0) {
        perror("epoll_ctl error");
        return 1;
    }

    // Main event loop: handle incoming connections and data.
    for (;;) {
        epoll_event eventList[MAX_EVENTS];
        int readyEvents = epoll_wait(epollInstance, eventList, MAX_EVENTS, -1);
        if (readyEvents < 0) {
            perror("epoll_wait error");
            break;
        }
        for (int idx = 0; idx < readyEvents; ++idx) {
            if (eventList[idx].data.fd == serverSocket) {
                // Accept new client connections.
                int clientSocket = accept(serverSocket, nullptr, nullptr);
                if (clientSocket < 0) {
                    perror("accept error");
                    continue;
                }
                // Set the new socket to non-blocking mode.
                int flags = fcntl(clientSocket, F_GETFL, 0);
                fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
                
                epoll_event clientEvent;
                clientEvent.events = EPOLLIN | EPOLLET;
                clientEvent.data.fd = clientSocket;
                if (epoll_ctl(epollInstance, EPOLL_CTL_ADD, clientSocket, &clientEvent) < 0) {
                    perror("epoll_ctl add client error");
                    close(clientSocket);
                }
            } else {
                // Process incoming data from an already-connected client.
                handleClientRequest(eventList[idx].data.fd, database);
            }
        }
    }
    close(serverSocket);
    return 0;
}
// ----- End main.cpp -----