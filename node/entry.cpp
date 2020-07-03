#include "app_delegate.hpp"
sgns::AppDelegate g_app_delegate;
int main (int argc, char * const * argv)
{
    int result (0);
    g_app_delegate.init();
    g_app_delegate.run();
    return result;
}