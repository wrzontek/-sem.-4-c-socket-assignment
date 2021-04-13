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

namespace fs = std::filesystem;

#define DEFAULT_PORT 8080

struct correlated_resource {
    correlated_resource(std::string &path, std::string &ip, int port):
                        path(std::move(path)), ip(std::move(ip)), port(port) {}

    fs::path path;
    std::string ip;
    int port;
};

//for (const auto & entry : fs::directory_iterator(fs::current_path()))
//    std::cout << entry.path() << std::endl;

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4)
        return EXIT_FAILURE;

    int my_port = DEFAULT_PORT;

    if (argc == 4)
        my_port = atoi(argv[3]);

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
    else {
        return EXIT_FAILURE;
    }

    for (correlated_resource &r : correlated_resources) {
        std::cout << "path: " << r.path << " ip: " << r.ip << " port: " << r.port << std::endl;
    }

    printf("my port: %d\n", my_port);

    return 0;
}
