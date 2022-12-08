#include <iostream>

#include "server.h"
#include <commlib/commlib.h>
#include <commlib/uvx.h>
#include <commlib/pack.h>
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
        uvcomms4::Server server(uvcomms4::config::get_default());
        //auto testc_res = std::async(std::launch::async, test_connection);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        //auto f1 = server.send(1, "ABCDEFGH"s);
        // server.send(1, "ABCDEFGH"s, [](int r){
        //     std::cout << "Lambda send result " << r << std::endl;
        // });

        //std::this_thread::sleep_for(std::chrono::seconds(1));
        //std::cout << "Send result: " << f1.get() << std::endl;
        //testc_res.get();
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
