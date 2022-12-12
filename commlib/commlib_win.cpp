#include "commlib.h"

namespace uvcomms4
{

    namespace {
        using namespace std::literals;

        Config get_default_config()
        {
            /* need to make sure the socket path length does not exceed sockaddr_un limits (around 108 characters) */
            return Config{
                .socket_directory = ""s, // leaving blank for now
                .lock_file_name = "uvcomms4.lock",
                .socket_file_name = "uvcomms4"
            };
        }
    }

    Config const& Config::get_default()
    {
        static const Config def_config{ get_default_config() };
        return def_config;
    }

    int ensure_socket_directory_exists(Config const&)
    {
        return 0;
    }

    int delete_socket_file(Config const&)
    {
        return 0;
    }

    std::string pipe_name(Config const& cfg)
    {
        return R"(\\.\pipe\)"s + cfg.socket_file_name;
    }

    void adjust_resource_limits()
    {

    }

}