#include "SampleServer.h"

namespace svr
{

    void SampleServer::onMessage(uvcomms4::Descriptor aDescriptor, uvcomms4::Collector &aCollector)
    {
        // reminder: UI thread

        // we MUST extract the message here; otherwise, we'll have an infinite loop
        auto [status, message] = aCollector.getMessage<std::string>();
        if(status == uvcomms4::CollectorStatus::HasMessage)
            std::cout << "[SampleServer] MESSAGE: " << message << std::endl;
    }

    void SampleServer::onNewPipe(uvcomms4::Descriptor aDescriptor)
    {
        // reminder: UI thread
        std::cout << "[SampleServer] New pipe: " << aDescriptor << std::endl;
    }

    void SampleServer::onPipeClosed(uvcomms4::Descriptor aDescriptor, int aErrorCode)
    {
        // reminder: UI thread
        std::cout << "[SampleServer] Pipe closed: " << aDescriptor << "; error code " << aErrorCode << std::endl;
    }
}
