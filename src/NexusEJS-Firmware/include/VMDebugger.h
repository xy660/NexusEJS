#pragma once

#include "VM.h"

#if VM_DEBUGGER_ENABLED

#pragma message("Contain debugger interface!!The VM will suspend at the first instruction!") 

#include "PlatformImpl.h"

void Debugger_CheckPoint(VMWorker* worker,uint32_t eip,uint16_t packageId);


#endif

