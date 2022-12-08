#pragma once

#include <commlib/server.h>

namespace svr
{
    class SampleServer : public uvcomms4::Server
    {
    public:
        SampleServer(uvcomms4::config const & aConfig) :
            Server(aConfig)
        {}

    private:
        /// called when there's a new full message available
        void onMessage(uvcomms4::Descriptor aDescriptor, uvcomms4::Collector & aCollector) override;

        /// called when a new pipe was connected; IO thread
        void onNewPipe(uvcomms4::Descriptor aDescriptor) override;

        /// called when a pipe was closed, either because of an error or normally IO thread
        void onPipeClosed(uvcomms4::Descriptor aDescriptor, int aErrorCode) override;
    };

}
