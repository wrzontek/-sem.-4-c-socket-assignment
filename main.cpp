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

    fs::current_path("/cygdrive/c/Users/Adrian/CLionProjects/SiK/");    // tymczasowe

    if (!fs::exists(path_to_dir) || !fs::exists(path_to_servers_file))
        return EXIT_FAILURE;

    path_to_dir = fs::absolute(path_to_dir);
    std::cout << path_to_dir << std::endl;


    std::ifstream servers_file (path_to_servers_file);
    std::string line;
    std::vector<correlated_resource> correlated_resources;  // lepiej set sortowany po zasobie ale nie trzeba

    if (servers_file.is_open()) {
        std::string path, ip, port;
        while ( getline (servers_file,line) ) {
            std::stringstream ss = std::stringstream(line);
            ss >> path >> ip >> port;
            correlated_resources.emplace_back(path, ip, atoi(port.c_str()));
        }
        servers_file.close();
    }
    else
        return EXIT_FAILURE;

    for (correlated_resource &r : correlated_resources)
        std::cout << "path: " << r.path << " ip: " << r.ip << " port: " << r.port << std::endl;

    printf("my port: %d\n", my_port);

    // przygotowujemy przydatne regexy
    static const std::regex request_line(R"(\w+ /[a-zA-Z0-9.-/]+ (HTTP\/1.1)\r\n)");
    // ^^^ potem sprawdzamy czy GET|HEAD
    static const std::regex status_line(R"((HTTP\/1.1) \d{3} \w+\r\n)");

    static const std::regex header_field(R"(((Connection: *close *)|(Content-Length: *\d+ *))\r\n)", std::regex::icase);

    static const std::regex empty_line("\r\n");

    std::string test1("GET /plik HTTP/1.1\r\n");
    std::string test2("HTTP/1.1 404 niema\r\n");
    std::string test3("CoNNection: close\r\n");
    std::string test4("\r\n");
    if (std::regex_match(test1, request_line))
        std::cout << "match 1\n";
    if (std::regex_match(test2, status_line))
        std::cout << "match 2\n";
    if (std::regex_match(test3, header_field))
        std::cout << "match 3\n";
    if (std::regex_match(test4, empty_line))
        std::cout << "match 4\n";



    return 0;
}
