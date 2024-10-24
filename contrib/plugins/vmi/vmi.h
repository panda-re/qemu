#ifndef VMI_H
#define VMI_H

#include "vmi_types.h"

QPP_FUN_PROTOTYPE(vmi, VmiProc*, get_current_process, void);
QPP_FUN_PROTOTYPE(vmi, VmiProc*, get_process, const VmiProcHandle*);
QPP_FUN_PROTOTYPE(vmi, VmiProcHandle*, get_current_process_handle, void);

struct get_process_data {
  VmiProc **p;
  const VmiProcHandle *h;
};

#endif