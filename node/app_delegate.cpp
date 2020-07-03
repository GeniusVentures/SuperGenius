#include "app_delegate.hpp"
#include <iostream>
namespace sgns
{
    AppDelegate::AppDelegate (){

    }
    AppDelegate::~AppDelegate (){

    }
    void AppDelegate::init(){
        std::cout << "--------------AppDelegate::init()---------------" << std::endl;
    }
    void AppDelegate::run(){
        std::cout << "--------------AppDelegate::run()---------------" << std::endl;
    }
    void AppDelegate::exit(){
        std::cout << "--------------AppDelegate::exit()---------------" << std::endl;
    }
} // namespace sgns
