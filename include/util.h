#ifndef OMCI_HOOK_UTIL_H
#define OMCI_HOOK_UTIL_H

#include "hook.h"

#define DLSYM(handle, fnptr, name) (*(void **)&(fnptr) = dlsym((handle), (name)))

#define LOG  hook_log_prn
#define LOGW hook_log_wrn
#define LOGE hook_log_err

#define OMCI_FRAME_MAX 2048

#endif
