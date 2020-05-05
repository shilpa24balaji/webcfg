#ifndef _WEBCONFIG_LOG_H_
#define _WEBCONFIG_LOG_H_

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <cimplog.h>

#define WEBCFG_LOGGING_MODULE                     "WEBCONFIG"
/**
 * @brief Enables or disables debug logs.
 */
#ifdef BUILD_YOCTO

#define WEBCFG_RDK_LOGGING_MODULE                 "LOG.RDK.WEBCONFIG"

#define WebConfigLog(...)       __cimplog_rdk_generic(WEBCFG_RDK_LOGGING_MODULE, WEBCFG_LOGGING_MODULE, LEVEL_INFO, __VA_ARGS__)

#define WebcfgError(...)	__cimplog_rdk_generic(WEBCFG_RDK_LOGGING_MODULE, WEBCFG_LOGGING_MODULE, LEVEL_ERROR, __VA_ARGS__)
#define WebcfgInfo(...)		__cimplog_rdk_generic(WEBCFG_RDK_LOGGING_MODULE, WEBCFG_LOGGING_MODULE, LEVEL_INFO, __VA_ARGS__)
#define WebcfgDebug(...)	__cimplog_rdk_generic(WEBCFG_RDK_LOGGING_MODULE, WEBCFG_LOGGING_MODULE, LEVEL_INFO, __VA_ARGS__)

#else
#define WebConfigLog(...)       printf(__VA_ARGS__)

#define WebcfgError(...)	printf(__VA_ARGS__)
#define WebcfgInfo(...)		printf(__VA_ARGS__)
#define WebcfgDebug(...)	printf(__VA_ARGS__)

#endif



#endif /* _WEBCONFIG_LOG_H_ */
