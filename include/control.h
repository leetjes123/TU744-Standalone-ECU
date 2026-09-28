#ifndef LRE_CONTROL_H
#define LRE_CONTROL_H
#include "ecu.h"
void engine_state_update(u32 now, const Rotation *r, const u8 *c);
void limits_update(u32 now, const Rotation *r, const u8 *c, EnginePlan *p);
void acceleration_update(u32 now, u16 dt, u16 rpm, const u8 *c);
void lambda_update(u32 now, const u8 *c, const EnginePlan *p);
void idle_update(u32 now, u16 dt, const Rotation *r, const u8 *c);
void fuel_plan(u32 now, const Rotation *r, const u8 *c, EnginePlan *p);
void auxiliary_update(u32 now, const Rotation *r, const u8 *c);
void fan_update(const u8 *c);
#endif
