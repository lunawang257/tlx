// cpu_compatibility.hpp
#pragma once

#if __APPLE__
#include <thread>

// Declare a thread-local variable to store the local thread ID
extern thread_local int local_thread_id;

// Define a replacement for sched_getcpu on Apple systems
inline int sched_getcpu() {
    return local_thread_id;
}

#else
#include <sched.h> // Include the standard Linux header for sched_getcpu

#endif