#include "Cariboulite.hpp"
#include "cariboulite_config/cariboulite_config_default.h"

#include <SoapySDR/Logger.hpp>
#include <mutex>
#include <cstddef>

//========================================================
int soapy_sighandler(int signum)
{
    SoapySDR_logf(SOAPY_SDR_DEBUG, "Received signal %d", signum);
    switch (signum)
    {
        case SIGINT: printf("Caught SIGINT\n"); break;
        case SIGTERM: printf("Caught SIGTERM\n"); break;
        case SIGABRT: printf("Caught SIGABRT\n"); break;
        case SIGILL: printf("Caught SIGILL\n"); break;
        case SIGSEGV: printf("Caught SIGSEGV\n"); break;
        case SIGFPE:  printf("Caught SIGFPE\n"); break;
        default: printf("Caught Unknown Signal %d\n", signum); return -1; break;
    }

    return -1;
}

static std::mutex sessionMutex;
static size_t sessionCount = 0;

SoapyCaribouliteSession::SoapyCaribouliteSession(void)
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    printf("SoapyCaribouliteSession, sessionCount: %d\n", sessionCount);
    if (sessionCount == 0)
    {
        CARIBOULITE_CONFIG_DEFAULT(temp);
        memcpy(&cariboulite_sys, &temp, sizeof(cariboulite_sys));
        
        int ret = cariboulite_init_driver(&cariboulite_sys, (void*)soapy_sighandler, NULL);
        if (ret != 0)
        {
            SoapySDR::logf(SOAPY_SDR_ERROR, "cariboulite_init_driver() failed");
        }
    }
    sessionCount++;
}

SoapyCaribouliteSession::~SoapyCaribouliteSession(void)
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    printf("~SoapyCaribouliteSession, sessionCount: %d\n", sessionCount);
    sessionCount--;
    if (sessionCount == 0)
    {
        cariboulite_release_driver(&cariboulite_sys);
    }
}
