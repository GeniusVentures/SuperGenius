#ifndef SUPERGENIUS_NODE_APP_DELEGATE_HPP
#define SUPERGENIUS_NODE_APP_DELEGATE_HPP
#include <boost/program_options.hpp>
#include <iostream>


namespace boost
{
namespace filesystem
{
	class path;
}
}

namespace sgns
{
class node_flags;
}

namespace sgns
{
    namespace application
    {
        class BlockProducingNodeApplication;
        class BlockValidatingNodeApplication;
        class AppConfigurationImpl;
    } // namespace application
    
    /**
     * @class SuperGenius AppDelegate class
     *        daemon node instance
     *        used in all platforms including android and iOS
     */

class AppDelegate {
public:
	explicit AppDelegate ();
	~AppDelegate ();
    /**
     * @brief Initialize  of SuperGenius daemon node.
     * @param 
     * @return int error code
     */
    int init(int argc, char * const * argv);
    /**
     * @brief main loop of SuperGenius daemon node
     * @param 
     * @return no return
     */
    void run(boost::filesystem::path const &/*, sgns::node_flags const & flags*/);
    /**
     * @brief  exit from main loop of SuperGenius daemon node
     * @param 
     * @return no return
     */
    void exit();
public:
    // boost::program_options::variables_map vm;
private:
    void init_node(int argc, char * const * argv);
    std::shared_ptr<application::BlockProducingNodeApplication> app_production;
    std::shared_ptr<application::BlockValidatingNodeApplication> app_production;
    std::shared_ptr<application::AppConfigurationImpl> configuration;
};
}
#endif