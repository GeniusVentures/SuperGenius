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
        class ValidatingNodeApplication;
        class AppConfigurationImpl;
    } // namespace application
    
    /**
     * @brief App delegate for the SuperGenius daemon node.
     *
     * Coordinates lifecycle for the node across platforms (including Android
     * and iOS).
     */

class AppDelegate {
public:
	explicit AppDelegate ();
	~AppDelegate ();
    /**
     * @brief Initializes the SuperGenius daemon node.
     * @param argc Count of CLI arguments.
     * @param argv Vector of CLI arguments.
     * @return Error code (0 on success).
     */
    int init(int argc, char * const * argv);
    /**
     * @brief Runs the main loop of the SuperGenius daemon node.
     */
    void run(/*boost::filesystem::path const &, sgns::node_flags const & flags*/);
    /**
     * @brief Signals the node to stop and exit the main loop.
     */
    void exit();
public:
    // boost::program_options::variables_map vm;
private:
/**
 * @brief Initializes the node internals.
 * @param argc Count of CLI arguments.
 * @param argv Vector of CLI arguments.
 */
    void init_node(int argc, char * const * argv);
    std::shared_ptr<application::BlockProducingNodeApplication> app_production;
    std::shared_ptr<application::ValidatingNodeApplication> app_validating;
    std::shared_ptr<application::AppConfigurationImpl> configuration;
};
}  // namespace sgns
#endif
