/* ---- BENCHMARK Parameters ---- */
// Benchmark Types:
// 1 -> %50 1us, %50 10us
// 2 -> %95	0.5us, %5 500us

// With Schedule:
// 0 -> Posted Interrupt (Default)
// 1 -> Yield
// 2 -> None (Cause Hol-block)
/* ----------------------------- */

#define BENCHMARK_STOP_AT_PACKET     1000000000000
#define BENCHMARK_DURATION_US        1000000 * 10 
#define DB_NUM_KEYS                  15000
#define CPU_FREQ_GHZ                 2.5

#ifndef SCHEDULE_METHOD
#define SCHEDULE_METHOD              METHOD_CONCORD
#endif

// Set to -1 to run in infinite loop
#define BENCHMARK_CREATE_NO_PACKET   -1

// Schedule Methods
#define METHOD_PI       0
#define METHOD_YIELD    1
#define METHOD_NONE     2
#define METHOD_CONCORD  3

// Debug Methods
#define LATENCY_DEBUG   1

// Dispatcher do work
#ifndef DISPATCHER_DO_WORK
#define DISPATCHER_DO_WORK 0
#endif

// If 0, runs leveldb. If 1 runs simpleloop
#ifndef RUN_UBENCH
#define RUN_UBENCH      1  
#endif

#if RUN_UBENCH == 1
    // Different workload mixes 
    #ifndef BENCHMARK_TYPE
    #define BENCHMARK_TYPE 5
    #endif

    #if BENCHMARK_TYPE == 0                      // 100% 100us.
    #define BENCHMARK_DB_GET_SPIN   62   
    #define BENCHMARK_DB_GET_NS     1000
    #define BENCHMARK_DB_ITERATOR_SPIN   6200  
    #define BENCHMARK_DB_ITERATOR_NS     100000
    #define MU                         0.01
    #elif  BENCHMARK_TYPE == 1                  // 50% 1us, 50% 100us
    #define BENCHMARK_DB_GET_SPIN   62   
    #define BENCHMARK_DB_GET_NS     1000
    #define BENCHMARK_DB_ITERATOR_SPIN   6200  
    #define BENCHMARK_DB_ITERATOR_NS     100000
    #define MU                         0.0198               
    #elif  BENCHMARK_TYPE == 2                  // 99.5% 0.5us, 0.5% 500us
    #define BENCHMARK_DB_GET_SPIN   27 
    #define BENCHMARK_DB_GET_NS     500
    #define BENCHMARK_DB_ITERATOR_SPIN   32000 
    #define BENCHMARK_DB_ITERATOR_NS     500000
    #define MU                         0.333611
    #elif  (BENCHMARK_TYPE == 3) || (BENCHMARK_TYPE == 4)    // Fixed 1us or exp 1us
    #define BENCHMARK_DB_GET_SPIN   62   
    #define BENCHMARK_DB_GET_NS     1000
    #define BENCHMARK_DB_ITERATOR_SPIN   6200  
    #define BENCHMARK_DB_ITERATOR_NS     100000
    #define MU                         1.0  
    #elif (BENCHMARK_TYPE == 5)     // TPCC workload
    #define BENCHMARK_DB_GET_SPIN   365   
    #define BENCHMARK_DB_GET_NS     5700
    #define BENCHMARK_DB_ITERATOR_SPIN   390  
    #define BENCHMARK_DB_ITERATOR_NS     6000
    #define BENCHMARK_DB_PUT_SPIN   1280   
    #define BENCHMARK_DB_PUT_NS     20000
    #define BENCHMARK_DB_DELETE_SPIN   5650  
    #define BENCHMARK_DB_DELETE_NS     88000
    #define BENCHMARK_DB_SEEK_SPIN   6500  
    #define BENCHMARK_DB_SEEK_NS     100000
    #define MU                        0.0181593666013
    #endif
#else
    #define BENCHMARK_TYPE 1 // Always. We only work with a 50-50 split
    #define BENCHMARK_DB_GET_SPIN   27 
    #define BENCHMARK_DB_GET_NS     1500  // Random get costs 1.5us
    #define BENCHMARK_DB_ITERATOR_SPIN   30000 
    #define BENCHMARK_DB_ITERATOR_NS     644000 // Scan of 15k keys costs ~644us
    #define MU                         0.0030511
#endif