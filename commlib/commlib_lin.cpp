#include "commlib.h"

#include <unistd.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <cassert>

namespace uvcomms4
{
    namespace {
        using namespace std::literals;

        Config get_default_config()
        {
            /* need to make sure the socket path length does not exceed sockaddr_un limits (around 108 characters) */
            return Config{
                .socket_directory = "/run/user/"s + std::to_string(getuid()) + "/com.myself.uvcomms4"s,
                .lock_file_name = "uvcomms4.lock",
                .socket_file_name = "uvcomms4.socket"
            };
        }
    }

    Config const & Config::get_default()
    {
        static const Config def_config { get_default_config() };
        return def_config;
    }


    int ensure_socket_directory_exists(Config const & aConfig)
    {
        int r;
        do {
            r = mkdir(aConfig.socket_directory.c_str(), 0777);
        } while(r < 0 && errno == EINTR);

        if(r < 0 && errno != EEXIST)
            return errno;

        do {
            r = chmod(aConfig.socket_directory.c_str(), 0777);
        } while(r < 0 && errno == EINTR); // unlike Linux, macOS man page states EINTR may happen

        /* Depending on the layout, chmod() may fail even if everything is actually good,
        e.g. the directory was created by another user and already has the 777 permissions,
        so we don't consider chmod() failure fatal because it is supposed to have been created
        by ourselves and already have the right permissions
        */

        return 0;
    }


    int delete_socket_file(Config const & aConfig)
    {
        int r;
        std::string fpath = pipe_name(aConfig);

        do {
            r = unlink(fpath.c_str());
        } while(r < 0 && errno == EINTR); // maybe EINTR check is superfluous here but won't harm

        bool succeeded = (r == 0) || errno == ENOENT;

        return succeeded ? 0 : errno;
    }

    std::string pipe_name(Config const & aConfig)
    {
        std::string result = aConfig.socket_directory + "/" + aConfig.socket_file_name;
        assert(result.size() < sizeof(sockaddr_un::sun_path));
        return result;
    }

    void configure_signals()
    {
        signal(SIGPIPE, SIG_IGN);
    }

}