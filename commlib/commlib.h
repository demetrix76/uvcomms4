#pragma once

#include <string>
#include <system_error>
#include <cstdint>

namespace uvcomms4
{

// pipe descriptor
using Descriptor = std::int64_t;

struct Config
{
    std::string  socket_directory; // for monitoring purposes
    std::string  lock_file_name; // under socket_directory
    std::string  socket_file_name; // under socket_directory on UNIX; must be decorated with "\\.\pipe\" on Windows

    static Config const & get_default();
};

/** Makes sure the socket directory exists and has apppropriate rights.
Definitely important for UNIXes. On Windows, we _might_ need this depending on
how we decide to signal the server availability
Returns 0 on success or POSIX error code on failure
*/
int ensure_socket_directory_exists(Config const &);

/** Probably a no-op on Windows, but on UNIX the server needs to delete it
to avoid bind() error.
Returns 0 on success or POSIX error code on failure
*/
int delete_socket_file(Config const &);

/** Formats the pipe name:
 on UNIX, combines the socket directory with the socket file;
 on Windows, prepends socket file name with "\\.\pipe\"
*/
std::string pipe_name(Config const &);

/** Only makes sense on UNIX: raises the maximum number of file descriptors
 * (on macOS this seems to be set to 256 per process by default);
 * Required for the stress test
*/
void adjust_resource_limits();

/** On UNIX we need to block SIGPIPE to prevent the app from closing
* when the pipe is closed on the other side
*/
void configure_signals();

}