#include "app_delegate.hpp"
#include "application/impl/app_config_impl.hpp"
#include "application/impl/block_producing_node_application.hpp"
#include "base/logger.hpp"
#include <boost/process/child.hpp>
#include <lib/utility.hpp>
#include "secure/utility.hpp"
#include <csignal>
#include <iostream>
#include "cli.hpp"
#include "daemonconfig.hpp"

namespace
{
void my_abort_signal_handler (int signum)
{
	std::signal (signum, SIG_DFL);
	sgns::dump_crash_stacktrace ();
	// sgns::create_load_memory_address_files ();
}
}
namespace sgns
{
    using sgns::application::AppConfiguration;
    using sgns::application::AppConfigurationImpl;
    AppDelegate::AppDelegate (){
        std::cout << "--------------AppDelegate::AppDelegate()---------------" << std::endl;
    }

    AppDelegate::~AppDelegate (){
        std::cout << "--------------AppDelegate::~AppDelegate()---------------" << std::endl;
    }

    int AppDelegate::init(int argc, char * const * argv){
        std::cout << "--------------AppDelegate::init()---------------" << std::endl;
        sgns::set_umask ();
        boost::program_options::options_description description ("Command line options");
        // clang-format off
        description.add_options ()
            ("help", "Print out options")
            ("version", "Prints out version")
            ("config", boost::program_options::value<std::vector<std::string>>()->multitoken(), "Pass node configuration values. This takes precedence over any values in the configuration file. This option can be repeated multiple times.")
            ("daemon", "Start node daemon")
            ("debug_block_count", "Display the number of block");
        // clang-format on
        sgns::add_node_options (description);
        sgns::add_node_flag_options (description);
        boost::program_options::variables_map vm;
        try
        {
            boost::program_options::store (boost::program_options::parse_command_line (argc, argv, description), vm);
        }
        catch (boost::program_options::error const & err)
        {
            std::cerr << err.what () << std::endl;
            return 1;
        }
        boost::program_options::notify (vm);

        // auto network (vm.find ("network"));
        // if (network != vm.end ())
        // {
        //     auto err (sgns::network_constants::set_active_network (network->second.as<std::string> ()));
        //     if (err)
        //     {
        //         std::cerr << sgns::network_constants::active_network_err_msg << std::endl;
        //         std::exit (1);
        //     }
        // }

        // auto data_path_it = vm.find ("data_path");
        // if (data_path_it == vm.end ())
        // {
        //     std::string error_string;
        //     if (!sgns::migrate_working_path (error_string))
        //     {
        //         std::cerr << error_string << std::endl;

        //         return 1;
        //     }
        // }
        // boost::filesystem::path data_path ((data_path_it != vm.end ()) ? data_path_it->second.as<std::string> () : sgns::working_path ());
	    // auto ec = sgns::handle_node_options (vm);

        auto logger = base::createLogger("SuperGenius block node: ");
        configuration = std::make_shared<AppConfigurationImpl>(logger);
        init_production_node(argc, argv);
    }

    void AppDelegate::run(boost::filesystem::path const & data_path/*, sgns::node_flags const & flags*/){
        std::cout << "--------------AppDelegate::run()---------------" << std::endl;
        	// Override segmentation fault and aborting.
	    std::signal (SIGSEGV, &my_abort_signal_handler);
	    std::signal (SIGABRT, &my_abort_signal_handler);
        
        boost::filesystem::create_directories (data_path);
	    boost::system::error_code error_chmod;
        // sgns::set_secure_perm_directory (data_path, error_chmod);

        // sgns::daemon_config config (data_path);
        app_production->run();
    }

    void AppDelegate::exit(){
        std::cout << "--------------AppDelegate::exit()---------------" << std::endl;

    }

    void AppDelegate::init_production_node(int argc, char * const * argv){

        configuration->initialize_from_args(
            AppConfiguration::LoadScheme::kBlockProducing, argc,(char**) argv);
        app_production =
            std::make_shared<application::BlockProducingNodeApplication>(
                std::move(configuration));
    }

} // namespace sgns
