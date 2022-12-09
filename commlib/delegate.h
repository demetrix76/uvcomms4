#pragma once

#include "commlib.h"
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




}
