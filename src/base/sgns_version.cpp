#include "sgns_version.hpp"

unsigned long SuperGenius_version_num(void)
{
    return SUPERGENIUS_VERSION_NUMBER;
}

unsigned int SuperGenius_version_major(void)
{
    return SUPERGENIUS_VERSION_MAJOR;
}

unsigned int SuperGenius_version_minor(void)
{
    return SUPERGENIUS_VERSION_MINOR;
}

unsigned int SuperGenius_version_patch(void)
{
    return SUPERGENIUS_VERSION_PATCH;
}

const char *SuperGenius_version_pre_release(void)
{
    return SUPERGENIUS_VERSION_PRE_RELEASE;
}

const char *SuperGenius_version_build_metadata(void)
{
    return SUPERGENIUS_VERSION_BUILD_METADATA;
}
