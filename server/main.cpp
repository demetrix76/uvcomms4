#include <iostream>

#include "server.h"
#include <commlib/commlib.h>
#include <commlib/uvx.h>
#include <vector>

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

    connect(fd, (sockaddr const *)&addr, SUN_LEN(&addr));

    constexpr std::size_t sz = 256 * 1024;
    std::vector<char> buffer;
    buffer.resize(sz);

    write(fd, std::data(buffer), sz);

}

int main(int, char*[])
{
    std::cout << "Hi there\n";

    try
    {
        uvcomms4::Server server(uvcomms4::config::get_default());
        test_connection();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    catch(std::exception &e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
