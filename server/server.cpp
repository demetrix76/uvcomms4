#include "server.h"

#include <commlib/uvx.h>
#include <iostream>
#include <type_traits>

namespace uvcomms4
{
    Server::Server(config const &aConfig) :
        Streamer(aConfig)
    {
        std::promise<void> initPromise;
        auto initFuture = initPromise.get_future();

        mThread = std::thread{
            [pms = std::move(initPromise), this]() mutable {
                threadFunction(std::move(pms));
            }
        };

        try
        {
            initFuture.get();
        }
        catch(...)
        {
            mThread.join();
            throw;
        }
    }

    Server::~Server()
    {
        request_stop();
        mThread.join();
    }


    void Server::threadFunction(std::promise<void> aInitPromise)
    {
        uvx::UVLoop theLoop;
        bool async_initialized = false;
        bool listener_initialized = false;
        try
        {
            // [TODO] acquire file lock first

            if(int r = delete_socket_file(mConfig); r != 0)
                throw std::system_error(std::error_code(r, std::system_category()), "Cannot delete socket file");

            if(int r = ensure_socket_directory_exists(mConfig); r != 0)
                throw std::system_error(std::error_code(r, std::system_category()), "Cannot create socket directory");

            theLoop.init();

            if(int r = streamer_init(theLoop); r < 0)
                throw std::system_error(std::error_code(-r, std::system_category()), "Streamer initialization failed");

            int r = uv_pipe_init(theLoop, &mListeningPipe, 0);
            mListeningPipe.data = this;

            if(r < 0)
                throw std::system_error(std::error_code(-r, std::system_category()), "Listening pipe initialization failed");
            listener_initialized = true;

            r = uv_pipe_bind(&mListeningPipe, pipe_name(mConfig).c_str());
            if(r < 0)
                throw std::system_error(std::error_code(-r, std::system_category()), "Listening pipe bind failed");

            // todo chmod the pipe

            r = uv_listen(reinterpret_cast<uv_stream_t*>(&mListeningPipe), 128,
                [](uv_stream_t* server, int status) {
                    reinterpret_cast<Server*>(server->data)->onConnection(server, status);
                }
            );

            if (r < 0)
                throw std::system_error(std::error_code(-r, std::system_category()), "Listening pipe listen failed");
        }
        catch(...)
        {
            if(listener_initialized)
                uvx::uv_close(mListeningPipe);

            streamer_deinit();

            if(theLoop.initialized())
                // there cannot be any requests incoming yet as listen() hasn't succeeded
                uv_run(theLoop, UV_RUN_NOWAIT);

            aInitPromise.set_exception(std::current_exception());
            return;
        }

        aInitPromise.set_value();

        std::cout << "Server loop running...\n";
        uv_run(theLoop, UV_RUN_DEFAULT);
        std::cout << "Server loop done, cleaning up\n";

        uvx::uv_close(mListeningPipe);

        streamer_deinit();

        uv_walk(theLoop, [](uv_handle_t *aHandle, void*){
            if(!uv_is_closing(aHandle))
                UVPipe::fromHandle(aHandle)->close();
        }, nullptr);

        // let the loop do the clean-up
        // seems there's no guarantee that the loop fill finish all its tasks in one iteration
        // as there may be unfinished requests
        int maxcount = 100;
        while(uv_run(theLoop, UV_RUN_NOWAIT) && 0 < (--maxcount))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

    }


    void Server::onConnection(uv_stream_t* aServer, int aStatus) // called on the server thread
    {
        std::cout << "Incoming connection\n";
        if(aStatus < 0)
        {
            uvx::report_uv_error(std::cerr, aStatus, "WARNING: incoming connection failed");
            return;
        }

        UVPipe *client = new UVPipe(this);

        if(int r = client->init(aServer->loop, 0); r < 0)
        {
            delete client;
            uvx::report_uv_error(std::cerr, r, "WARNING: cannot initialize a pipe for incoming connection");
            return;
        }

        // once initialized, we should no longer delete the UVPipe object upon failure,
        // but rather let uv_close_cb do it

        if(int r = uv_accept(aServer, *client); r < 0)
        {
            uvx::report_uv_error(std::cerr, r, "WARNING: cannot accept an incoming connection");
            client->close(); // will be deleted on the next loop iteration
            return;
        }

        if(int r = client->read_start(); r < 0)
        {
            uvx::report_uv_error(std::cerr, r, "WARNING: cannot start reading from an incoming connection");
            client->close(); // will be deleted on the next loop iteration
            return;
        }

    }

}
