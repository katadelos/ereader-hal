#ifndef ER_CAPABILITIES_H
#define ER_CAPABILITIES_H
#include "ereader_hal_types.h"

enum er_capability_reason er_capability_reason_from_error(int error);
void er_capability_finish(struct er_capability_info *info, int result);
#endif
