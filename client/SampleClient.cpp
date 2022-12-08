#include "SampleClient.h"

namespace clt
{
    void SampleClient::onMessage(uvcomms4::Descriptor aDescriptor, uvcomms4::Collector &aCollector)
    {
        // reminder: UI thread

        // we MUST extract the message here; otherwise, we'll have an infinite loop
        auto [status, message] = aCollector.getMessage<std::string>();
        if(status == uvcomms4::CollectorStatus::HasMessage)
            std::cout << "[SampleClient] MESSAGE: " << message << std::endl;
    }

    void SampleClient::onNewPipe(uvcomms4::Descriptor aDescriptor)
    {
        // reminder: UI thread
        std::cout << "[SampleClient] New pipe: " << aDescriptor << std::endl;
    }

    void SampleClient::onPipeClosed(uvcomms4::Descriptor aDescriptor, int aErrorCode)
    {
        // reminder: UI thread
        std::cout << "[SampleClient] Pipe closed: " << aDescriptor << "; error code " << aErrorCode << std::endl;
    }

}
