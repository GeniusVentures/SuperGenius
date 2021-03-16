#include "app_delegate.hpp"
#include <boost/program_options.hpp>
#include <sstream>
#include <iostream> 
sgns::AppDelegate g_app_delegate;
int main (int argc, char * const * argv)
{
	std::cout << "--------------main()---------------" << std::endl;
    int result (0);
    g_app_delegate.init(argc, argv);
    // boost::program_options::variables_map vm;
    // auto data_path_it = vm.find ("data_path");
	// if (data_path_it == vm.end ())
	// {
	// 	std::string error_string;
	// 	if (!sgns::migrate_working_path (error_string))
	// 	{
	// 		std::cerr << error_string << std::endl;

	// 		return 1;
	// 	}
	// }
    // boost::filesystem::path data_path ((data_path_it != vm.end ()) ? data_path_it->second.as<std::string> () : sgns::working_path ());
    g_app_delegate.run(/*data_path*/);
    g_app_delegate.exit();
    return result;
}