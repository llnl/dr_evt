#define DR_EVT_HAS_CONFIG 1
#include "sim/scheduler_base.hpp"
#include <iostream>
struct Probe : dr_evt::SchedulerBase { static void sizes() { std::cout << "record=" << sizeof(Planned_Start) << " vector=" << sizeof(m_planned_starts) << " timestamp=" << sizeof(m_planned_at) << " base=" << sizeof(SchedulerBase) << "\n"; } };
int main() { Probe::sizes(); }
