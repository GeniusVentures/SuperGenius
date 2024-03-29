/**
* Header file for base class for processors. Derived classes will handle processing various
* types of AI/ML processing as needed. Give this to a ProcessingCoreImpl.
* @author Justin Church
*/

namespace sgns::processing
{
    class ProcessingProcessor
    {
    public:
        virtual ~ProcessingProcessor()
        {
        }

        /** Process data
        */
        virtual std::vector<uint8_t> StartProcessing() = 0;
    };
}