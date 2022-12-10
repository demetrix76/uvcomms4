#include "piper.h"
#include "final_act.h"
#include <optional>
#include <system_error>
#include <cassert>

namespace uvcomms4
{
    Piper::Piper(PiperDelegate::pointer aDelegate) :
        mDelegate(aDelegate)
    {
        std::promise<void> initPromise;
        std::future<void> initFuture = initPromise.get_future();

        mIOThread = std::thread([pms = std::move(initPromise), this]()mutable {
            threadFunction(std::move(pms));
        });

        final_act fin_thread{[this] { mIOThread.join(); }};
        initFuture.get();

        final_act fin_stop([this] { requestStop(); } );
        mDelegate->Startup(this);

        fin_stop.cancel();
        fin_thread.cancel();
    }


    Piper::~Piper()
    {
        mDelegate->Shutdown();
        requestStop();
        mIOThread.join();
    }


    void Piper::threadFunction(std::promise<void> aInitPromise)
    {
        mIOThreadId = std::this_thread::get_id();

        std::optional<detail::Loop<Piper>> theLoop;
        bool async_initialized = false;

        try
        {
            theLoop.emplace(this);

            if(int r = uv_async_init(*theLoop, &mAsyncTrigger, detail::cb<Piper>::async); r < 0)
                throw std::system_error(std::error_code(-r, std::system_category()), "Cannot initialize uv_async handle");

            async_initialized = true; // async.data must remain null to comply with the loop's cleanup
        }
        catch(...)
        {
            if(async_initialized)
                uv_close(reinterpret_cast<uv_handle_t*>(&mAsyncTrigger), nullptr);

            theLoop.reset();

            aInitPromise.set_exception(std::current_exception());
        }

        aInitPromise.set_value();

        mRunningLoop = *theLoop;
        std::cout << "Piper Loop running...\n";
        uv_run(*theLoop, UV_RUN_DEFAULT);
        std::cout << "Piper Loop done\n";
        mRunningLoop = nullptr;

    }


    void Piper::requestStop()
    {
        std::cout << "requestStop\n";
        {
            std::lock_guard lk(mMx);
            mStopFlag = true;
        }
        triggerAsync();
    }


    void Piper::onAsync(uv_async_t *aAsync)
    {
        requireIOThread();

        bool stopRequested = false;
        {
            std::lock_guard lk(mMx);
            stopRequested = mStopFlag;
        }

        if(stopRequested)
        {
            uv_stop(aAsync->loop);
            processPendingRequests(true); // true means Abort
        }
        else
        {
            processPendingRequests(false); // false means process normally
        }

    }


    void Piper::requireIOThread()
    {
#ifdef UVCOMMS_THREAD_CHECKS
        assert(std::this_thread::get_id() == mIOThreadId);
#endif
    }

    void Piper::requireNonIOThread()
    {
#ifdef UVCOMMS_THREAD_CHECKS
        assert(std::this_thread::get_id() != mIOThreadId);
#endif
    }

    Descriptor Piper::nextDescriptor()
    {
        return mNextDescriptor++;
    }

    void Piper::triggerAsync()
    {
        uv_async_send(&mAsyncTrigger);
    }

    void Piper::postRequest(requests::Request::pointer aRequest)
    {
        {
            std::lock_guard lk(mMx);
            mPendingRequests.emplace_back(std::move(aRequest));
        }
        triggerAsync();
    }

    void Piper::processPendingRequests(bool aAbort)
    {
        requireIOThread();

        mPendingRequestsTemporary.clear();
        {
            std::lock_guard lk(mMx);
            mPendingRequestsTemporary.swap(mPendingRequests);
        }

        if(aAbort)
        {
            for(std::unique_ptr<requests::Request> & req: mPendingRequestsTemporary)
                req->abort();
        }
        else
        {
            // once dispatched, request lifetime is the handler's responsibility
            for(std::unique_ptr<requests::Request> & req: mPendingRequestsTemporary)
                req.release()->dispatchToHandler(this);
        }

        mPendingRequestsTemporary.clear();

    }

    //================================================================================================================
    // LISTENING
    //================================================================================================================

    void Piper::handleListenRequest(requests::ListenRequest * aListenRequest)
    {
        requireIOThread();
        std::unique_ptr<requests::ListenRequest> theReq(aListenRequest);

        auto [listeningPipe, errCode] = [&, this]() -> std::tuple<detail::UVPipe*, int> {
            detail::UVPipe* listeningPipe = detail::UVPipe::init(nextDescriptor(), mRunningLoop, false);
            if(!listeningPipe)
                return { nullptr, UV_ERRNO_MAX };
            if(int r = listeningPipe->bind(theReq->listenAddress.c_str()); r < 0)
                return { listeningPipe, r };
            if(int r = listeningPipe->listen<Piper>(); r < 0)
                return { listeningPipe, r };
            return { listeningPipe, 0 };
        }();

        if(errCode != 0)
        {
            listeningPipe->close();
            theReq->fulfill({0, errCode});
        }
        else
        {
            theReq->fulfill({listeningPipe->descriptor(), 0});
        }
    }

    void Piper::onConnection(uv_stream_t *aServer, int aStatus) // incoming connection
    {
        requireIOThread();
        std::cout << "Incoming connection\n";

        if(aStatus < 0)
        {
            std::cerr << "WARNING: error in incoming connection: "
                << std::error_code(-aStatus, std::system_category()).message()
                << std::endl;
            return;
        }

        detail::UVPipe *client = detail::UVPipe::init(nextDescriptor(), mRunningLoop, false);
        detail::UVPipe *server = detail::UVPipe::fromHandle(aServer);

        if(!client)
        {
            std::cerr << "WARNING: error creating a new pipe for incoming connection\n";
            return;
        }

        if(int r = uv_accept(aServer, *client); r < 0)
        {
            std::cerr << "WARNING: error accepting incoming connection: "
                << std::error_code(-r, std::system_category()).message()
                << std::endl;
            client->close();
        }

        if(int r = client->read_start<Piper>(); r < 0)
        {
            std::cerr << "WARNING: error reading from incoming connection: "
                << std::error_code(-r, std::system_category()).message()
                << std::endl;
        }

        mDelegate->onNewConnection(server->descriptor(), client->descriptor());
    }


    //================================================================================================================
    // READING
    //================================================================================================================
    void Piper::onRead(uv_stream_t *aStream, ssize_t aNread, const uv_buf_t *aBuf)
    {
        std::cout << "Read " << aNread << " bytes\n";
        std::free(aBuf->base);
    }

    void Piper::onAlloc(uv_handle_t *aHandle, size_t aSuggested_size, uv_buf_t *aBuf)
    {
        aBuf->base = (char*)std::malloc(65536);
        aBuf->len = 65536;
    }
}
