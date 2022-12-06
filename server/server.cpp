#include "server.h"

#include <commlib/uvx.h>
#include <iostream>

namespace uvcomms4
{

    Server::Server(config const &aConfig) :
        mConfig(aConfig)
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
        mStopRequested.store(true);
        uv_async_send(&mAsyncTrigger);
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
            int r = uv_async_init(theLoop, &mAsyncTrigger,
                [](uv_async_t *aAsync) {
                    static_cast<Server*>(aAsync->data)->onAsync(aAsync);
                }
            );
            mAsyncTrigger.data = this;
            if (r < 0)
                throw std::system_error(std::error_code(-r, std::system_category()), "Async trigger initialization failed");
            async_initialized = true;

            r = uv_pipe_init(theLoop, &mListeningPipe, 0);
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
            if(async_initialized)
                uvx::uv_close(mAsyncTrigger);
            if(theLoop.initialized())
                uv_run(theLoop, UV_RUN_NOWAIT);

            aInitPromise.set_exception(std::current_exception());
            return;
        }

        aInitPromise.set_value();

        std::cout << "Server loop running...\n";
        uv_run(theLoop, UV_RUN_DEFAULT);
        std::cout << "Server loop done, cleaning up\n";

        uvx::uv_close(mListeningPipe);
        uvx::uv_close(mAsyncTrigger);

        uv_walk(theLoop, [](uv_handle_t *aHandle, void*){
            if(!uv_is_closing(aHandle))
                uv_close(aHandle, nullptr); // [TODO] !!! free handle memory
        }, nullptr);

        uv_run(theLoop, UV_RUN_NOWAIT);

    }

    void Server::onAsync(uv_async_t *aAsync) // called on the server thread
    {
        if(mStopRequested.load())
        {
            uv_stop(aAsync->loop);
            return;
        }
        // [TODO] check for write requests etc.
    }

    void Server::onConnection(uv_stream_t* aServer, int aStatus) // called on the server thread
    {
        std::cout << "Incoming connection\n";
    }
}
