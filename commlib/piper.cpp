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
        initFuture.get(); // UB sanitizer barks on this (invalid vptr etc) if we get an exception from the IO thread;
        // however, that seems to be a false positive. Possible explanation: https://stackoverflow.com/questions/57294792/c-ubsan-produces-false-positives-with-derived-objects


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

            return;
        }

        aInitPromise.set_value();

        mRunningLoop = *theLoop;
        //std::cout << "Piper Loop running...\n";
        uv_run(*theLoop, UV_RUN_DEFAULT);
        //std::cout << "Piper Loop done\n";
        mRunningLoop = nullptr;

    }


    void Piper::requestStop()
    {
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

    void Piper::triggerAsync() // any thread
    {
        uv_async_send(&mAsyncTrigger);
    }

    void Piper::postRequest(requests::Request::pointer aRequest) // any thread
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

    void Piper::onClosed(Descriptor aPipe, int aErrCode)
    {
        requireIOThread();
        pipeUnregister(aPipe);
        mDelegate->onPipeClosed(aPipe, aErrCode);
    }

    void Piper::pipeRegister(UVPipe *aPipe)
    {
        requireIOThread();
        auto found = mPipes.find(aPipe->descriptor());
        assert(found == mPipes.end()); // this would be Piper logic error
        mPipes.insert({ aPipe->descriptor(), aPipe });
    }

    void Piper::pipeUnregister(Descriptor aDescriptor)
    {
        requireIOThread();
        auto found = mPipes.find(aDescriptor);
        if(found != mPipes.end())
            mPipes.erase(found);
    }

    Piper::UVPipe *Piper::pipeGet(Descriptor aDescriptor)
    {
        requireIOThread();
        auto found = mPipes.find(aDescriptor);
        return found == mPipes.end() ?
            nullptr : found->second;
    }

//================================================================================================================
// LISTENING
//================================================================================================================

    void Piper::handleListenRequest(requests::ListenRequest * aListenRequest)
    {
        requireIOThread();
        std::unique_ptr<requests::ListenRequest> theReq(aListenRequest);

        auto [listeningPipe, errCode] = [&, this]() -> std::tuple<UVPipe*, int> {
            UVPipe* listeningPipe = UVPipe::init(nextDescriptor(), mRunningLoop, false);
            if(!listeningPipe)
                return { nullptr, UV_ERRNO_MAX };
            if(int r = listeningPipe->bind(theReq->listenAddress.c_str()); r < 0)
                return { listeningPipe, r };
            if(int r = listeningPipe->listen(); r < 0)
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
            pipeRegister(listeningPipe);
            // NOTE this triggers ThreadSanitizer:
            // either there exists a data race in libstdc++ std::promise/future,
            // or this is a false positive
            // TODO verify with clang/libc++
            theReq->fulfill({listeningPipe->descriptor(), 0});
            // the problem doesn't happen if we don't free the listenRequest
            // (which contains the lambda which contains the promise)
        }
    }


    void Piper::onConnection(uv_stream_t *aServer, int aStatus) // incoming connection
    {
        requireIOThread();

        if(aStatus < 0)
        {
            std::cerr << "WARNING: error in incoming connection: "
                << std::error_code(-aStatus, std::system_category()).message()
                << std::endl;
            return;
        }

        UVPipe *client = UVPipe::init(nextDescriptor(), mRunningLoop, false);
        UVPipe *server = UVPipe::fromHandle(aServer);

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

        if(int r = client->read_start(); r < 0)
        {
            std::cerr << "WARNING: error reading from incoming connection: "
                << std::error_code(-r, std::system_category()).message()
                << std::endl;
        }

        pipeRegister(client);

        mDelegate->onNewConnection(server->descriptor(), client->descriptor());
    }


//================================================================================================================
// READING
//================================================================================================================
    void Piper::onRead(uv_stream_t *aStream, ssize_t aNread, const uv_buf_t *aBuf)
    {
        requireIOThread();
        UVPipe *thePipe = UVPipe::fromHandle(aStream);

        if(aNread == UV_EOF)
        {
            ReadBuffer::memfree(aBuf->base);

            // this callback does not add new data so there's no need to check the Collector for complete messages
            // but we might want to know if there's an incomplete message?
            if(thePipe->collector().contains(1))
                std::cerr << "WARNING: end of stream reached but there's a (possibly) icomplete message in the read buffer!\n";

            thePipe->close(0);
            // immediately delete the pipe from the descriptor table?
        }
        else if(aNread < 0)
        {
            ReadBuffer::memfree(aBuf->base);
            thePipe->close((int)aNread);
            // immediately delete the pipe from the descriptor table?
        }
        else
        {
            // N.B. zero length reads are possible, avoid adding such buffers
            // zero-length messages are allowed but they will have at least 8 bytes of header
            Collector & collector = thePipe->collector();

            if(aNread > 0)
                collector.append(ReadBuffer{aBuf->base, (std::size_t)aNread});

            while(collector.status() == CollectorStatus::HasMessage)
               mDelegate->onMessage(thePipe->descriptor(), collector);

            if(collector.status() == CollectorStatus::Corrupt)
                thePipe->close(UV_ECONNABORTED);
            }

    }

    void Piper::onAlloc(uv_handle_t *aHandle, size_t aSuggested_size, uv_buf_t *aBuf)
    {
        requireIOThread();
        UVPipe *thePipe = UVPipe::fromHandle(aHandle);
        std::size_t to_allocate = thePipe->recvBuferSize();
        if(to_allocate == 0)
            to_allocate = aSuggested_size;

        aBuf->base = ReadBuffer::memalloc(to_allocate);
        aBuf->len = aBuf->base ? to_allocate : 0;
    }


//================================================================================================================
// CONNECTING
//================================================================================================================

    void Piper::handleConnectRequest(requests::ConnectRequest *aConnectRequest)
    {
        requireIOThread();
        std::unique_ptr<requests::ConnectRequest> theReq(aConnectRequest);

        UVPipe *connectedPipe = UVPipe::init(nextDescriptor(), mRunningLoop, false);
        if(!connectedPipe)
        {
            theReq->fulfill({0, UV_ERRNO_MAX});
            return;
        }

        uv_pipe_connect(&theReq->uv_connect_req, *connectedPipe,
            theReq->connectAddress.c_str(),
            &detail::cb<Piper>::connect);

        theReq.release();
    }


    void Piper::onConnect(uv_connect_t *aReq, int aStatus) // outgoing connection
    {
        requireIOThread();

        std::unique_ptr<requests::ConnectRequest> theReq(requests::ConnectRequest::fromUVReq(aReq));
        UVPipe *connectedPipe = UVPipe::fromHandle(aReq->handle);

        if(aStatus < 0)
        {
            theReq->fulfill({0, aStatus});
            connectedPipe->close();
            return;
        }
        else
        {
            if(int r = connectedPipe->read_start(); r < 0)
            {
                theReq->fulfill({0, r});
                connectedPipe->close();
            }

            pipeRegister(connectedPipe);

            theReq->fulfill({connectedPipe->descriptor(), 0});
        }
    }

//================================================================================================================
// WRITING
//================================================================================================================

    void Piper::handleWriteRequest(requests::WriteRequest * aReq)
    {
        requireIOThread();
        std::unique_ptr<requests::WriteRequest> theReq(aReq);

        UVPipe *thePipe = pipeGet(theReq->pipeDescriptor);
        if(!thePipe)
        {// the pipe is gone or has never existed
            theReq->fulfill(UV_ENOTCONN);
            return;
        }

        if(thePipe->isListener())
        {
            theReq->fulfill(UV_ENOTSUP);
            return;
        }

        uv_buf_t buffers[] {
            uv_buf_init(theReq->header, sizeof(theReq->header)),
            uv_buf_init(const_cast<char*>(theReq->data()), static_cast<unsigned>(theReq->size()))
        };

        int r = uv_write(&theReq->uv_write_request, *thePipe,
            buffers, std::size(buffers), &detail::cb<Piper>::write);

        if(0 == r)
            theReq.release();
        else
            theReq->fulfill(r);
    }


    void Piper::onWrite(uv_write_t *aReq, int aStatus)
    {
        requireIOThread();
        std::unique_ptr<requests::WriteRequest> theReq(
            static_cast<requests::WriteRequest*>(aReq->data)
        );

        theReq->fulfill(aStatus);
    }

//================================================================================================================
// CLOSING
//================================================================================================================

    void Piper::handleCloseRequest(requests::CloseRequest *aReq)
    {
        requireIOThread();
        std::unique_ptr<requests::CloseRequest> theReq(aReq);

        UVPipe *thePipe = pipeGet(theReq->descriptor);
        if(!thePipe)
        {
            theReq->fulfill(UV_ENOTCONN);
        }
        else
        {
            // request will be fulfilled when the pipe is actually destroyed
            if(!thePipe->setCloseRequest(std::move(theReq)))
                theReq->fulfill(UV_ENOTSUP); // unless another request has already been issued
            thePipe->close();
        }

    }
}
