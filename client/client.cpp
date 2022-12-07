#include "client.h"
#include <commlib/uvx.h>
#include <commlib/commlib.h>

namespace uvcomms4
{
    Client::Client(config const & aConfig) :
        Streamer(aConfig)
    {
        std::promise<void> initPromise;
        auto initFuture = initPromise.get_future();

        mThread = std::thread {
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

    Client::~Client()
    {
        request_stop();
        mThread.join();
    }

    void Client::threadFunction(std::promise<void> aInitPromise)
    {
        UVLoop theLoop;
        try
        {
            if(int r = ensure_socket_directory_exists(mConfig); r != 0)
                throw std::system_error(std::error_code(r, std::system_category()), "Cannot create socket directory");

            theLoop.init(); // throws on failure

            if(int r = streamer_init(theLoop); r < 0)
                throw std::system_error(std::error_code(-r, std::system_category()), "Streamer initialization failed");

        }
        catch(...)
        {
            aInitPromise.set_exception(std::current_exception());
            streamer_deinit();
            if(theLoop.initialized())
                // there cannot be any requests incoming yet as we haven't attempted to connect yet
                uv_run(theLoop, UV_RUN_NOWAIT);
            return;
        }

        aInitPromise.set_value();

        std::cout << "Client loop running...\n";
        uv_run(theLoop, UV_RUN_DEFAULT);
        std::cout << "Client loop done, cleaning up\n";

        streamer_deinit();

        uv_walk(theLoop, [](uv_handle_t *aHandle, void*){
            if(!uv_is_closing(aHandle)) {
                UVPipe::fromHandle(aHandle)->close();
                std::cerr << "WARNING: found a stray pipe!\n";
            }
        }, nullptr);

        // let the loop do the clean-up
        // seems there's no guarantee that the loop fill finish all its tasks in one iteration
        // as there may be unfinished requests
        int maxcount = 100;
        while(uv_run(theLoop, UV_RUN_NOWAIT) && 0 < (--maxcount))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

    }
}