#pragma once

#include <commlib/client.h>

namespace clt
{

    class SampleClient: public uvcomms4::Client
    {
    public:
        using Client::Client;


    private:
        /// called when there's a new full message available
        void onMessage(uvcomms4::Descriptor aDescriptor, uvcomms4::Collector & aCollector) override;

        /// called when a new pipe was connected; IO thread
        void onNewPipe(uvcomms4::Descriptor aDescriptor) override;

        /// called when a pipe was closed, either because of an error or normally IO thread
        void onPipeClosed(uvcomms4::Descriptor aDescriptor, int aErrorCode) override;
    };

}
