#pragma once
#include <openxr/openxr.h>
XrInstance snap_quest_xr_instance();
XrSystemId snap_quest_xr_system();
void snap_quest_set_performance(XrSession);
float snap_quest_display_refresh(XrSession,bool request=false);
double snap_quest_monotonic_time(XrTime);
