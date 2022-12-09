#include "piper.h"
#include "final_act.h"
#include <optional>
#include <system_error>

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

        std::cout << "Piper Loop running...\n";
        uv_run(*theLoop, UV_RUN_DEFAULT);
        std::cout << "Piper Loop done\n";

    }


    void Piper::requestStop()
    {
        std::cout << "requestStop\n";
        {
            std::lock_guard lk(mMx);
            mStopFlag = true;
        }
        uv_async_send(&mAsyncTrigger);
    }


    void Piper::onAsync(uv_async_t *aAsync)
    {
        std::cout << "onAsync\n";
        bool stopRequested = false;
        {
            std::lock_guard lk(mMx);
            stopRequested = mStopFlag;
        }

        if(stopRequested)
        {
            uv_stop(aAsync->loop);
        }

    }
}
