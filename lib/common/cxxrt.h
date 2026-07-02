#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* C++ runtime support for freestanding environment */
void __cxa_pure_virtual(void);
void __cxa_deleted_virtual(void);

#ifdef __cplusplus
}
#endif