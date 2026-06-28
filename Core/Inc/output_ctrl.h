#ifndef OUTPUT_CTRL_H
#define OUTPUT_CTRL_H

#include <stdbool.h>

void OutputCtrl_Init(void);
void OutputCtrl_SetEnabled(bool enabled);
bool OutputCtrl_IsEnabled(void);

#endif /* OUTPUT_CTRL_H */
