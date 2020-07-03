#ifndef SUPERGENIUS_NODE_APP_DELEGATE_HPP
#define SUPERGENIUS_NODE_APP_DELEGATE_HPP
namespace sgns
{
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
     * @return no return
     */
    void init();
    /**
     * @brief main loop of SuperGenius daemon node
     * @param 
     * @return no return
     */
    void run();
    /**
     * @brief  exit from main loop of SuperGenius daemon node
     * @param 
     * @return no return
     */
    void exit();
};
}
#endif