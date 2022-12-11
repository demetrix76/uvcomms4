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

    class Client;
    class Server;

    class Delegate
    {
    public:
        using pointer = std::shared_ptr<Delegate>;

        virtual ~Delegate() {}

        /** Called when a new complete incoming message becomes available.
         *  Called on the IO thread and the supplied Collector must only be accessed from the IO thread;
         *  Extract the message before moving to another thread.
         */
        virtual void onMessage(uvcomms4::Descriptor aDescriptor, uvcomms4::Collector & aCollector) = 0;

        /// called on the IO thread when a new pipe was connected; either from the Listener (Server)
        /// or in response to Connect(Client) IO thread
        virtual void onNewPipe(uvcomms4::Descriptor aDescriptor) = 0;

        /// called on the IO thread when a pipe was closed, either because of an error or normally IO thread
        virtual void onPipeClosed(uvcomms4::Descriptor aDescriptor, int aErrorCode) = 0;

        /// called before stopping the Destructor thread (normally must match Constructor)
        virtual void onShutdown() = 0;
    };


    class ServerDelegate: public Delegate
    {
    public:
        using pointer = std::shared_ptr<ServerDelegate>;
        /// Called after the IO thread has started; Constructor thread
        /// normally, the IO loop will only start after this method finishes,
        /// so we shouldn't make any blocking calls that depend on the IO loop;
        /// or, call unlockIO() first once we're done initializing
        virtual void onStartup(Server *aServer) = 0;
    };


    class ClientDelegate: public Delegate
    {
    public:
        using pointer = std::shared_ptr<ClientDelegate>;
        /// Called after the IO thread has started; Constructor thread
        /// (see ServerDelegate::onStartup)
        virtual void onStartup(Client *aClient) = 0;
    };

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
         *  Not allowed to throw
        */
        virtual void Shutdown() noexcept = 0;

        /** Called on the IO thread when a new connection is accepted.
        */
        virtual void onNewConnection(Descriptor aListener, Descriptor aPipe) = 0;

        /** Called on the IO thread when a pipe has been closed (error/EOF/piper stopped).
         *  Currently, this is called on Listener pipes too.
         *  May be called for descriptors not known to the delegate if the pipe had been created
         *  but the subsequent connection request failed.
        */
        virtual void onPipeClosed(Descriptor aPipe, int aErrCode) = 0;

        /** Called when a new complete incoming message becomes available.
         *  Called on the IO thread and the supplied Collector must only be accessed from the IO thread;
         *  Extract the message before moving to another thread.
         */
        virtual void onMessage(Descriptor aDescriptor, Collector & aCollector) = 0;
    };



}
