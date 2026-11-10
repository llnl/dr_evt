#define DR_EVT_HAS_CONFIG 1
#include "sim/scheduler_base.hpp"
#include <iostream>
struct Probe : dr_evt::SchedulerBase { static void sizes() { std::cout << "record=" << sizeof(Estimated_Start) << " vector=" << sizeof(m_estimated_starts) << " timestamp=" << sizeof(m_estimated_at) << " base=" << sizeof(SchedulerBase) << "\n"; } };
int main() { Probe::sizes(); }
