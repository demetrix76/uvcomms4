#include <iostream>

#include "SampleServer.h"
#include <vector>
#include <memory>

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>

void test_connection()
{
    std::cout << "TESTING CONNECTION\n";
    uvcomms4::config cfg = uvcomms4::config::get_default();
    std::string sock_path = uvcomms4::pipe_name(cfg);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path));

    // connect(fd, (sockaddr const *)&addr, SUN_LEN(&addr)); // SUN_LEN triggers UB sanitizer
    connect(fd, (sockaddr const *)&addr, sizeof(sockaddr_un));

    constexpr std::size_t sz = 256 * 1024;
    std::vector<char> buffer;
    buffer.resize(sz);

    write(fd, std::data(buffer), sz);

    char read_buffer[16];
    read(fd, read_buffer, 16);
}

int main(int, char*[])
{
    using namespace std::literals;
    std::cout << "Hi there\n";
    try
    {
        svr::SampleServer server(uvcomms4::config::get_default());

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::cout << "Hit Enter to stop...\n";
        std::string s;
        std::getline(std::cin, s, '\n');
    }
    catch(std::exception &e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
