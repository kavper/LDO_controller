#ifndef BLEEDER_H
#define BLEEDER_H

#include <stdbool.h>

void Bleeder_Init(void);
void Bleeder_Task1ms(void);
bool Bleeder_IsEnabled(void);

#endif /* BLEEDER_H */
