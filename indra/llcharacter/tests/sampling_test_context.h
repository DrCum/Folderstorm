#ifndef FS_SAMPLING_TEST_CONTEXT_H
#define FS_SAMPLING_TEST_CONTEXT_H
#include "fssamplingcontext.h"
#include <iostream>
namespace FSSamplingTest
{
inline bool supportedContext(const char* test)
{
    if (FSSampling::NumericContext::capture().isSupported()) return true;
    std::cout << "SKIP " << test << ": owned numeric context unsupported; normal direct playback remains available\n";
    return false;
}
}
#endif
