#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fstream>
#include <utility>
#include <vector>
#include <iterator>
#include <regex>

namespace fs = std::filesystem;

#define DEFAULT_PORT 8080
#define BUFFER_SIZE   2000
#define QUEUE_LENGTH 5

#define OK 200
#define OTHER_SERVER 302
#define INVALID_REQUEST 400
#define NOT_FOUND 404
#define SERVER_ERROR 500
#define UNSUPPORTED_OPERATION 501

namespace {
    struct correlated_resource {
        correlated_resource(std::string &path, std::string &ip, int port) :
                path(std::move(path)), ip(std::move(ip)), port(port) {}

        fs::path path;
        std::string ip;
        int port;
    };

    std::vector<std::string> tokenize_string(const std::string& s, const char delimeter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimeter))
            if (!token.empty())
                tokens.push_back(token);

        return tokens;
    }
}
//for (const auto & entry : fs::directory_iterator(fs::current_path()))
//    std::cout << entry.path() << std::endl;

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4)
        return EXIT_FAILURE;

    int my_port = DEFAULT_PORT;

    if (argc == 4) {
        std::string port_num_string(argv[3]);
        my_port = std::stoi(port_num_string);
    }
    fs::path path_to_dir = argv[1];
    fs::path path_to_servers_file = argv[2];

    //fs::current_path("/cygdrive/c/Users/Adrian/CLionProjects/SiK/");    // tymczasowe

    if (!fs::exists(path_to_dir) || !fs::exists(path_to_servers_file))
        return EXIT_FAILURE;

    path_to_dir = fs::absolute(path_to_dir);
    std::cout << path_to_dir << std::endl;


    std::ifstream servers_file (path_to_servers_file);
    std::string servers_line;
    std::vector<correlated_resource> correlated_resources;  // lepiej set sortowany po zasobie ale nie trzeba

    if (servers_file.is_open()) {
        std::string path, ip, port;
        while ( getline (servers_file, servers_line) ) {
            std::stringstream ss = std::stringstream(servers_line);
            ss >> path >> ip >> port;
            correlated_resources.emplace_back(path, ip, std::stoi(port));
        }
        servers_file.close();
    }
    else
        return EXIT_FAILURE;

    for (correlated_resource &r : correlated_resources)
        std::cout << "path: " << r.path << " ip: " << r.ip << " port: " << r.port << std::endl;

    printf("my port: %d\n", my_port);

    // przygotowujemy przydatne regexy
    static const std::regex request_line_regex(R"(\w+ /[a-zA-Z0-9.-/]+ (HTTP\/1.1)\r)");  //TODO na koniec \r\n
    // ^^^ potem sprawdzamy czy GET|HEAD
    //static const std::regex status_line_regex(R"((HTTP\/1.1) \d{3} \w+\r\n)");

    static const std::regex header_field_regex(R"(((Connection: *close *)|(Content-Length: *\d+ *))\r)", std::regex::icase);

    static const std::regex empty_line_regex("\r");

    std::string test1("GET /plik HTTP/1.1\r\n");
    std::string test2("HTTP/1.1 404 niema\r\n");
    std::string test3("CoNNection: close\r\n");
    std::string test4("\r\n");
    if (std::regex_match(test1, request_line_regex))
        std::cout << "match 1\n";
    //if (std::regex_match(test2, status_line_regex))
    //    std::cout << "match 2\n";
    if (std::regex_match(test3, header_field_regex))
        std::cout << "match 3\n";
    if (std::regex_match(test4, empty_line_regex))
        std::cout << "match 4\n";

    int sock, msg_sock;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    char buffer[BUFFER_SIZE];
    ssize_t len, snd_len;

    sock = socket(PF_INET, SOCK_STREAM, 0); // tworzymy IPv4 TCP socket
    if (sock < 0)
        return EXIT_FAILURE;

    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(my_port); // listening on port my_port

    if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
        return EXIT_FAILURE;

    if (listen(sock, QUEUE_LENGTH) < 0)
        return EXIT_FAILURE;

    printf("accepting client connections on port %hu\n", ntohs(server_address.sin_port));

    while (true) {
        client_address_len = sizeof(client_address);
        // get client connection from the socket
        msg_sock = accept(sock, (struct sockaddr *) &client_address, &client_address_len);
        if (msg_sock < 0)
            return EXIT_FAILURE;
        do {
            len = read(msg_sock, buffer, sizeof(buffer));
            if (len < 0) {
                return EXIT_FAILURE;    // moze inny error i wysylamy do typa coś
            }
            // if len == 0 to koniec połączenia
            else if (len > 0) {
                //std::stringstream ss(buffer);
                //std::string line;
                std::string received(buffer, len);
                std::vector<std::string> lines = tokenize_string(received, '\n');
                for (std::string &token : lines) {
                    std::cout << "line[i]: " << token << std::endl;
                }
                std::string &line = lines[0];
                // TODO wczytywanie po linii jak człowiek a nie

                if (!std::regex_match(line, request_line_regex)) {
                    std::string response = "HTTP/1.1 400 invalid_request_line\r\n";
                    snd_len = write(msg_sock, response.c_str(), response.length());
                    if (snd_len != response.length())
                        return EXIT_FAILURE;
                    break;
                }
                else {
                    std::cout << "GIT\n";
                    std::vector<std::string> line_tokens = tokenize_string(line, ' ');
                    for (std::string &token : line_tokens) {
                        std::cout << "token: " << token << std::endl;
                    }
                    if (line_tokens[0] != "GET" && line_tokens[0] != "HEAD") {
                        // UNSUPPORTED_OPERATION wysyłamy
                    }
                }

                //snd_len = write(msg_sock, buffer, len);
                //if (snd_len != len)
                //    return EXIT_FAILURE;

            }

        } while (len > 0);
        printf("ending connection\n");
        if (close(msg_sock) < 0)
            return EXIT_FAILURE;
    }

    return 0;
}
