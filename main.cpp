#include <iostream>
#include <filesystem>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fstream>
#include <vector>
#include <regex>
#include <set>
#include <fcntl.h>

namespace fs = std::filesystem;

#define DEFAULT_PORT 8080
#define BUFFER_SIZE   4096
#define QUEUE_LENGTH 5

#define GET false
#define HEAD true

#define IMMEDIATE 1
#define AFTER_RESPONSE 2

namespace {
    static const std::regex request_line_regex(R"(\w+ \/[a-zA-Z0-9./-]* (HTTP\/1.1)\r\n)");

    static const std::regex header_field_regex(R"([a-zA-Z0-9.-/-]+: *[a-zA-Z0-9.-/-]+ *\r\n)");

    struct correlated_resource {
        correlated_resource(std::string &path, std::string &ip, std::string &port) :
                path(std::move(path)), ip(std::move(ip)), port(std::move(port)) {}

        std::string path;
        std::string ip;
        std::string port;
    };

    bool get_correlated_resources(fs::path &path_to_servers_file, std::vector<correlated_resource> &correlated_resources) {
        std::ifstream servers_file(path_to_servers_file);
        std::string servers_line;
        if (servers_file.is_open()) {
            std::string path, ip, port;
            while (getline(servers_file, servers_line)) {
                std::stringstream ss = std::stringstream(servers_line);
                ss >> path >> ip >> port;
                correlated_resources.emplace_back(path, ip, port);
            }
            servers_file.close();
            return 0;
        } else
            return EXIT_FAILURE;
    }

    std::vector<std::string> tokenize_string(const std::string &s, const char delimeter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimeter))
            if (!token.empty())
                tokens.push_back(token);

        return tokens;
    }

    std::string my_getline(std::string &buf_str, int msg_sock, char *receive_buffer) {
        if (buf_str.empty())
            return buf_str;

        std::size_t pos = buf_str.find("\r\n");
        if (pos == std::string::npos) {
            // nie znaleziono \r\n, trzeba doczytać
            std::cout << "nie ma \\r\\n !\n";
            memset(receive_buffer, 0, BUFFER_SIZE);
            ssize_t len = read(msg_sock, receive_buffer, sizeof(receive_buffer));
            if (len < 0)
                return buf_str; // TODO ERROR
            if (len == 0)
                return buf_str;

            buf_str += std::string(receive_buffer, len);    // dodajemy do bufora nowo wczytaną porcję
            return my_getline(buf_str, msg_sock, receive_buffer);
        }
        else {
            std::string line = buf_str.substr(0, pos + 2);
            buf_str.erase(0, pos + 2);
            std::cout << "gotline:" << line;
            return line;
        }
    }

    bool send_error(int msg_sock, std::string error_msg, std::string error_num, bool close_conn) {
        std::string response = "HTTP/1.1 " + error_num + " " + error_msg + "\r\n";
        if (close_conn)
            response += "Connection: close\r\n\r\n";
        else
            response += "\r\n";

        ssize_t snd_len = write(msg_sock, response.c_str(), response.length());
        if (snd_len != response.length())
            return EXIT_FAILURE;
        return 0;
    }

    bool handle_headers(std::string &buf_str, int msg_sock, int &end_connection, char *receive_buffer) {
        std::set<std::string> seen_fields;

        while (true) {
            std::string line = my_getline(buf_str, msg_sock, receive_buffer);
            if (line == "\r\n")
                break;  // koniec headerów
            if (!std::regex_match(line, header_field_regex)) {
                if (send_error(msg_sock, "invalid_header_field", "400", true) != 0)
                    return EXIT_FAILURE;
                end_connection = IMMEDIATE;
                break;
            }
            std::transform(line.begin(), line.end(), line.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            std::vector<std::string> line_tokens = tokenize_string(line, ' ');
            if (line_tokens[0] == "connection:" || line_tokens[0] == "content-length:") {
                if (seen_fields.find(line_tokens[0]) != seen_fields.end()) {
                    if (send_error(msg_sock, "repeat_field_name", "400", true) != 0)
                        return EXIT_FAILURE;
                    end_connection = IMMEDIATE;
                    break;
                }

                seen_fields.insert(line_tokens[0]);
            }
            if (line_tokens[0] == "connection:" && line_tokens[1] == "close")
                end_connection = AFTER_RESPONSE;

            if (line_tokens[0] == "content-length:" && (line_tokens[1] != "0" && line_tokens[1] != "0\r\n")) {
                if (send_error(msg_sock, "non_empty_message_body", "400", true) != 0)
                    return EXIT_FAILURE;
                end_connection = IMMEDIATE;
                break;
            }
        }
        return 0;
    }
}

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

    if (!fs::exists(path_to_dir) || !fs::exists(path_to_servers_file))
        return EXIT_FAILURE;

    path_to_dir = fs::absolute(path_to_dir);

    std::vector<correlated_resource> correlated_resources;
    if (get_correlated_resources(path_to_servers_file, correlated_resources) != 0)
        return EXIT_FAILURE;

    int sock, msg_sock;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    char receive_buffer[BUFFER_SIZE];
    char send_buffer[BUFFER_SIZE];
    ssize_t len, snd_len;

    sock = socket(PF_INET, SOCK_STREAM, 0); // tworzymy IPv4 TCP socket
    if (sock < 0)
        return EXIT_FAILURE;

    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // słuchamy na wszystkich interfejsach
    server_address.sin_port = htons(my_port); // słuchamy na porcie my_port

    if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
        return EXIT_FAILURE;

    if (listen(sock, QUEUE_LENGTH) < 0)
        return EXIT_FAILURE;

    printf("accepting client connections on port %hu\n", ntohs(server_address.sin_port));

    while (true) {
        client_address_len = sizeof(client_address);

        msg_sock = accept(sock, (struct sockaddr *) &client_address, &client_address_len);
        if (msg_sock < 0)
            return EXIT_FAILURE;

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(msg_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

        do {
            memset(receive_buffer, 0, BUFFER_SIZE);
            len = read(msg_sock, receive_buffer, sizeof(receive_buffer));
            if (len < 0) {
                return EXIT_FAILURE;
            }
                // if len == 0 to koniec połączenia
            else if (len > 0) {
                int end_connection = false;
                std::string buf_str(receive_buffer, len);

                while (end_connection == false) {
                    std::string line = my_getline(buf_str, msg_sock, receive_buffer);
                    std::cout << "line:" << line;
                    if (line.empty())
                        break;

                    if (!std::regex_match(line, request_line_regex)) {
                        if (send_error(msg_sock, "invalid_request_line", "400", true) != 0)
                            return EXIT_FAILURE;
                        break;
                    }
                    std::cout << "GIT: " << line;
                    std::vector<std::string> line_tokens = tokenize_string(line, ' ');

                    if (line_tokens[0] != "GET" && line_tokens[0] != "HEAD") {
                        if (send_error(msg_sock, "unsupported_method", "501", true) != 0)
                            return EXIT_FAILURE;
                        break;
                    }
                    bool method = HEAD;
                    if (line_tokens[0] == "GET")
                        method = GET;

                    std::string filestr(line_tokens[1]);
                    // mamy metodę i nazwę pliku, teraz czytamy headery
                    if (handle_headers(buf_str, msg_sock, end_connection, receive_buffer) != 0)
                        return EXIT_FAILURE;

                    if (end_connection == IMMEDIATE)
                        break;

                    if (filestr == "/" || filestr.substr(1, 2) == "..") {
                        if (send_error(msg_sock, "file_not_found", "404", false) != 0)
                            return EXIT_FAILURE;
                        continue;
                    }

                    fs::path file(path_to_dir.string() + filestr);

                    if (!fs::exists(file)) {
                        file = fs::path(filestr);
                        bool found = false;
                        for (correlated_resource &r : correlated_resources) {
                            if (r.path == file) {
                                found = true;
                                static const std::string status_line = "HTTP/1.1 302 file_on_other_server\r\n";
                                std::string headers =
                                        "location: http://" + r.ip + ":" + r.port + r.path +
                                        "\r\nContent-Length: 0\r\n\r\n";
                                std::string response = status_line + headers;
                                std::cout << response;
                                if (write(msg_sock, response.c_str(), response.length()) != response.length())
                                    return EXIT_FAILURE;
                                continue;
                            }
                        }
                        if (!found) {
                            if (send_error(msg_sock, "file_not_found", "404", false) != 0)
                                return EXIT_FAILURE;
                            continue;
                        }
                    }

                    int fd = open(file.string().c_str(), O_RDONLY);
                    if (fd < 0) {
                        send_error(msg_sock, "500", "error_opening_file", false);
                        continue;
                    }

                    memset(send_buffer, 0, sizeof(send_buffer));
                    static const std::string status_line_ok = "HTTP/1.1 200 OK\r\n";
                    const std::string headers = "Content-Length: " + std::to_string(fs::file_size(file))
                                                + "\r\nContent-Type: application/octet-stream\r\n\r\n";
                    const std::string response = status_line_ok + headers;

                    if (method == HEAD) {
                        if (write(msg_sock, response.c_str(), response.length()) != response.length())
                            return EXIT_FAILURE;
                    } else {
                        strcpy(send_buffer, response.c_str());
                        snd_len = read(fd, send_buffer + response.size(),
                                       sizeof(send_buffer) - response.size());
                        snd_len += response.size();

                        if (write(msg_sock, send_buffer, snd_len) != snd_len)
                            return EXIT_FAILURE;

                        while (true) {  // wysyłanie reszty pliku jak pierwszy write nie wystarczył
                            memset(send_buffer, 0, sizeof(send_buffer));
                            snd_len = read(fd, send_buffer, sizeof(send_buffer));
                            if (snd_len == 0)
                                break;

                            if (write(msg_sock, send_buffer, snd_len) != snd_len)
                                return EXIT_FAILURE;
                        }
                    }
                    std::cout << response;
                }
            }

        } while (len > 0);
        printf("ending connection\n");
        if (close(msg_sock) < 0)
            return EXIT_FAILURE;
    }

    return 0;
}
