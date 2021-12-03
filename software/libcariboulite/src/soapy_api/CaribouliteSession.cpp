#include "Cariboulite.hpp"
#include "cariboulite_config/cariboulite_config_default.h"

#include <SoapySDR/Logger.hpp>
#include <mutex>
#include <cstddef>

std::mutex SoapyCaribouliteSession::sessionMutex;
size_t SoapyCaribouliteSession::sessionCount = 0;
cariboulite_st SoapyCaribouliteSession::cariboulite_sys = {0};

//========================================================
int soapy_sighandler(int signum)
{
    SoapySDR_logf(SOAPY_SDR_DEBUG, "Received signal %d", signum);
    switch (signum)
    {
        case SIGINT: printf("soapy_sighandler caught SIGINT\n"); break;
        case SIGTERM: printf("soapy_sighandler caught SIGTERM\n"); break;
        case SIGABRT: printf("soapy_sighandler caught SIGABRT\n"); break;
        case SIGILL: printf("soapy_sighandler caught SIGILL\n"); break;
        case SIGSEGV: printf("soapy_sighandler caught SIGSEGV\n"); break;
        case SIGFPE:  printf("soapy_sighandler caught SIGFPE\n"); break;
        default: printf("soapy_sighandler caught Unknown Signal %d\n", signum); return -1; break;
    }

    SoapySDR_logf(SOAPY_SDR_INFO, "soapy_sighandler killing soapy_cariboulite (cariboulite_release_driver)");
    std::lock_guard<std::mutex> lock(SoapyCaribouliteSession::sessionMutex);
    cariboulite_release_driver(&(SoapyCaribouliteSession::cariboulite_sys));
    SoapyCaribouliteSession::sessionCount = 0;
    return 0;
}


//========================================================
SoapyCaribouliteSession::SoapyCaribouliteSession(void)
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    //printf("SoapyCaribouliteSession, sessionCount: %ld\n", sessionCount);
    SoapySDR_logf(SOAPY_SDR_INFO, "SoapyCaribouliteSession, sessionCount: %ld", sessionCount);
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

//========================================================
SoapyCaribouliteSession::~SoapyCaribouliteSession(void)
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    //printf("~SoapyCaribouliteSession, sessionCount: %ld\n", sessionCount);
    SoapySDR_logf(SOAPY_SDR_INFO, "~SoapyCaribouliteSession, sessionCount: %ld", sessionCount);
    sessionCount--;
    if (sessionCount == 0)
    {
        cariboulite_release_driver(&cariboulite_sys);
    }
}
