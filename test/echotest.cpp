#include <gtest/gtest.h>

#include "echotest.h"

using namespace uvcomms4;


//N.B. Attaching to a process in linux requires disabling ptrace protection:
//"This is due to kernel hardening in Linux; you can disable this behavior by echo 0 > /proc/sys/kernel/yama/ptrace_scope or by modifying it in /etc/sysctl.d/10-ptrace.conf"
//https://stackoverflow.com/questions/19215177/how-to-solve-ptrace-operation-not-permitted-when-trying-to-attach-gdb-to-a-pro

/** The test itself is not perfect:
 *  - the clients tend to disconnect early (relying on the wrong counters)
 *  - close_count() was written without temporary connect() failures
 *  without ECONNREFUSED (i.e. under normal load) the test passes;
 *  in either case, only the test code fails. piper works OK.
*/

TEST(EchoTest, EchoTest1)
{
    configure_signals();
    Config const &cfg = Config::get_default();
    ensure_socket_directory_exists(cfg);
    delete_socket_file(cfg);
    std::string pipename = pipe_name(cfg);

    std::size_t workers_count = 3;
    std::size_t clients_per_worker = 3;
    std::size_t connections_per_client = 3;
    std::size_t messages_per_connection = 1000;


}