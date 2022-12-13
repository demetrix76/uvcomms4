#pragma once

#include "commlib.h"
#include "collector.h"
#include <memory>

namespace uvcomms4
{

    /* Problem: inheriting from Client/Server doesn't work well.
    We stop the loop in destructor and certain events may arrive when the descendant class has already been destroyed,
    so we can end up with a pure virtual call.
    Solution: use delegation instead
    */

    class Piper;

    class PiperDelegate
    {
    public:
        using pointer = std::shared_ptr<PiperDelegate>;

        virtual ~PiperDelegate() {}

        /** Called on the Constructor thread after the IO thread has started.
         *  This function is allowed to throw in case of errors —
         *  in this case, the loop will be stopped and Piper construction aborted.
         *  Piper does not issue any requests at its own volition so
         *  there will be no unexpected calls to the Delegate until our
         *  Startup method initiates something.
        */
        virtual void Startup(Piper * aPiper) = 0;

        /** Called on the Destructor thread before issuing a stop request for the IO thread.
         *  Normally, doesn't need to do anything. Any listeners and other open pipes will be closed automatically.
         *  Not allowed to throw.
        */
        virtual void Shutdown() noexcept = 0;

        /** Called on the IO thread when a new connection is accepted.
         *  Not allowed to throw.
        */
        virtual void onNewConnection(Descriptor aListener, Descriptor aPipe) noexcept = 0;

        /** Called on the IO thread when a pipe has been closed (error/EOF/piper stopped).
         *  Currently, this is called on Listener pipes too.
         *  May be called for descriptors not known to the delegate if the pipe had been created
         *  but the subsequent connection request failed.
         *  Not allowed to throw.
        */
        virtual void onPipeClosed(Descriptor aPipe, int aErrCode) noexcept = 0;

        /** Called when a new complete incoming message becomes available.
         *  Called on the IO thread and the supplied Collector must only be accessed from the IO thread;
         *  Extract the message before moving to another thread.
         *  Not allowed to throw.
         */
        virtual void onMessage(Descriptor aDescriptor, Collector & aCollector) noexcept = 0;
    };



}
