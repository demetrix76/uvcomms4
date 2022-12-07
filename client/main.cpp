#include <iostream>

#include "client.h"

int main(int, char*[])
{
    std::cout << "Hi, client here\n";

    uvcomms4::Client client(uvcomms4::config::get_default());
    std::this_thread::sleep_for(std::chrono::seconds(1));


    return 0;
}
