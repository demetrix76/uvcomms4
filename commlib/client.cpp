#include "client.h"
#include "uvx.h"
#include "commlib.h"

#include <iostream>
namespace uvcomms4
{
    Client::Client(config const & aConfig, ClientDelegate::pointer aDelegate) :
        Streamer(aConfig, aDelegate)
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

        try
        {
            aDelegate->onStartup(this);
        }

        catch(...)
        {
            request_stop();
            unlockIO();
            mThread.join();
            throw;
        }

        unlockIO();
    }

    Client::~Client()
    {
        mDelegate->onShutdown();
        request_stop();
        mThread.join();
    }


    void Client::threadFunction(std::promise<void> aInitPromise)
    {
        UVLoop theLoop;
        try
        {
            // todo setup availability monitor[s]... or do it on a higher level

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

        mLoopSemaphore.acquire();

        //std::cout << "Client loop running...\n";
        uv_run(theLoop, UV_RUN_DEFAULT);
        //std::cout << "Client loop done, cleaning up\n";

        streamer_deinit();

        int maxcount = 100;
        while(uv_run(theLoop, UV_RUN_NOWAIT) && 0 < (--maxcount))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        uv_walk(theLoop, [](uv_handle_t *aHandle, void*){
            if(!uv_is_closing(aHandle)) {
                UVPipe::fromHandle(aHandle)->close();
                std::cerr << "WARNING: found a stray pipe!\n";
            }
        }, nullptr);

        // let the loop do the clean-up
        // seems there's no guarantee that the loop fill finish all its tasks in one iteration
        // as there may be unfinished requests
        maxcount = 100;
        while(uv_run(theLoop, UV_RUN_NOWAIT) && 0 < (--maxcount))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

    }


    void Client::onAsync(uv_async_t *aAsync, bool aStopping)
    {
        if(aStopping)
            abortPendingConnectRequests(aAsync->loop);
        else
            processPendingConnectRequests(aAsync->loop);
    }


    void Client::onConnect(uv_connect_t *aReq, int aStatus) // connect callback
    {
        std::unique_ptr<detail::IConnectRequest> theReq { // take control over the pointer
            static_cast<detail::IConnectRequest*>(aReq->data) };

        UVPipe *connection = UVPipe::fromHandle(theReq->uv_connect_request.handle);
        if(aStatus < 0)
        {
            // do we need to close the pipe? or just deallocate it?
            theReq->complete({0, aStatus});
            connection->close(); // we need to call close anyway (confirmed by ASAN)
        }
        else
        {
            if(int r = connection->read_start(); r < 0)
            {
                theReq->complete({0, r});
                connection->close();
                return;
            }
            adopt(connection);
            theReq->complete({connection->descriptor(), 0});
        }
    }


    void Client::initiateConnect(detail::IConnectRequest::pointer aReq, uv_loop_t *aLoop)
    {
        UVPipe *connection = new UVPipe(this, next_descriptor());

        if(int r = connection->init(aLoop, 0); r < 0)
        {
            delete connection;
            aReq->complete({0, r});
            return;
        }

        // once uv_pipe_connect has been called, we should no longer
        // delete neither the request nor the pipe handle;
        // may only do this in onConnect()
        detail::IConnectRequest *theReq = aReq.release();
        uv_pipe_connect(&theReq->uv_connect_request, *connection, theReq->pipe_name.c_str(), &UVPipe::connect_cb);
    }


    void Client::processPendingConnectRequests(uv_loop_t * aLoop) // IO thread
    {
        mConnectQueueTemporary.clear();
        {
            std::lock_guard lk(mMx);
            mConnectQueueTemporary.swap(mConnectQueue);
        }

        for(std::unique_ptr<detail::IConnectRequest> & req: mConnectQueueTemporary)
            initiateConnect(std::move(req), aLoop);

        mConnectQueueTemporary.clear();
    }


    void Client::abortPendingConnectRequests(uv_loop_t * aLoop) // IO thread
    {
        mConnectQueueTemporary.clear();
        {
            std::lock_guard lk(mMx);
            mConnectQueueTemporary.swap(mConnectQueue);
        }

        for(std::unique_ptr<detail::IConnectRequest> & req: mConnectQueueTemporary)
            req->complete({0, UV_ECONNABORTED});

        mConnectQueueTemporary.clear();
    }

}