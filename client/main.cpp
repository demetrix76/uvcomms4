#include <iostream>

#include "SampleClient.h"

int main(int, char*[])
{
    std::cout << "Hi, client here\n";

    clt::SampleClient client(uvcomms4::config::get_default());
    std::this_thread::sleep_for(std::chrono::seconds(1));


    return 0;
}
