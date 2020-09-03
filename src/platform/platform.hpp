#ifndef PLATFORM_H 
#define PLATFORM_H

#if defined(_MSC_VER)
//    #include <BaseTsd.h>
//    typedef SSIZE_T ssize_t;    
    #define TEMPLATE_TO to
#else
    #define TEMPLATE_TO template to
#endif

#endif