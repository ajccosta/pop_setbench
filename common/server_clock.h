/*
 * File:   server_clock.h
 * Author: trbot
 *
 * Created on August 2, 2017, 6:37 PM
 */

#ifndef SERVER_CLOCK_H
#define SERVER_CLOCK_H

#if defined __x86_64__ && !defined CPU_FREQ_GHZ
#error "Must define CPU_FREQ_GHZ for server_clock.h on __x86_64__"
#endif

#include <time.h>
#include <sched.h>

inline uint64_t get_server_clock() {
#if defined(__i386__)
    uint64_t ret;
    __asm__ __volatile__("rdtsc" : "=A" (ret));
#elif defined(__x86_64__)
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t ret = ( (uint64_t)lo)|( ((uint64_t)hi)<<32 );
        ret = (uint64_t) ((double)ret / CPU_FREQ_GHZ);
#else
        timespec * tp = new timespec;
    clock_gettime(CLOCK_MONOTONIC, tp);
    uint64_t ret = tp->tv_sec * 1000000000 + tp->tv_nsec;
#endif
    return ret;
}

timespec getUptimeTimespec() {
    // size_t ___uptime_nanos_at_startTime = sched_clock();
    // size_t ___start_time_seconds = (int) (sched_clock());
    //printf("REALTIME_START_PERF_FORMAT=%lu%s%lu\n", std::chrono::duration_cast<std::chrono::seconds>(g->startTime).count(), ".", 0)
    // auto time_point = std::chrono::system_clock::now();
    timespec uptime;
    clock_gettime(CLOCK_MONOTONIC, &uptime);
    return uptime;
}
#define printUptimeStampForPERF(label) { \
    SOFTWARE_BARRIER; \
    timespec ___currts = getUptimeTimespec(); \
    SOFTWARE_BARRIER; \
    printf("REALTIME_%s_PERF_FORMAT=%ld%s%ld\n", (label), ___currts.tv_sec, ".", ___currts.tv_nsec); \
}

//class ClockSplitter {
//private:
//    uint64_t time;
//
//    inline uint64_t get_server_clock() {
//#if defined(__i386__)
//        uint64_t ret;
//        __asm__ __volatile__("rdtsc" : "=A" (ret));
//#elif defined(__x86_64__)
//        unsigned hi, lo;
//        __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
//        uint64_t ret = ( (uint64_t)lo)|( ((uint64_t)hi)<<32 );
//            ret = (uint64_t) ((double)ret / CPU_FREQ_GHZ);
//#else
//            timespec * tp = new timespec;
//        clock_gettime(CLOCK_REALTIME, tp);
//        uint64_t ret = tp->tv_sec * 1000000000 + tp->tv_nsec;
//#endif
//        return ret;
//    }
//
//public:
//    ClockSplitter() {}
//    void reset() {
//        time = get_server_clock();
//    }
//    uint64_t split() {
//        uint64_t old = time;
//        time = get_server_clock();
//        return time - old;
//    }
//};

#ifdef MEASURE_TIMELINE_STATS
    PAD;
    volatile bool ___timeline_use = 0;
    PAD;
    #define ___MIN_INTERVAL_DURATION 0
    #define TIMELINE_BLIP(tid, name) { \
        if (___timeline_use) { \
            uint64_t ___blipTime = get_server_clock(); \
            printf("timeline_blip_%s tid=%d start=%lu\n", (name), (tid), ___blipTime); \
        } \
    }
    #define TIMELINE_BLIP_Llu(tid, name, label_lu) { \
        if (___timeline_use) { \
            uint64_t ___blipTime = get_server_clock(); \
            printf("timeline_blip_%s tid=%d start=%lu label=%lu\n", (name), (tid), ___blipTime, (unsigned long) (label_lu)); \
        } \
    }
    #define TIMELINE_BLIP_Ls(tid, name, label_s) { \
        if (___timeline_use) { \
            uint64_t ___blipTime = get_server_clock(); \
            printf("timeline_blip_%s tid=%d start=%lu label=%s\n", (name), (tid), ___blipTime, (unsigned long) (label_s)); \
        } \
    }
    #define TIMELINE_START_C(tid, condition) \
        uint64_t ___startTime; \
        if (___timeline_use && (condition)) { \
            ___startTime = get_server_clock(); \
        }
    #define TIMELINE_START(tid) TIMELINE_START_C((tid), true)
    #define TIMELINE_END_C(tid, name, condition) { \
        if (___timeline_use && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - ___startTime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                printf("timeline_%s tid=%d start=%lu end=%lu\n", (name), (tid), ___startTime, ___endTime); \
            } \
        } \
    }
    #define TIMELINE_END_C_Llu(tid, name, condition, label_lu) { \
        if (___timeline_use && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - ___startTime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                printf("timeline_%s tid=%d start=%lu end=%lu label=%lu\n", (name), (tid), ___startTime, ___endTime, (unsigned long) (label_lu)); \
            } \
        } \
    }
    #define TIMELINE_END_C_Ls(tid, name, condition, label_s) { \
        if (___timeline_use && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - ___startTime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                printf("timeline_%s tid=%d start=%lu end=%lu label=%s\n", (name), (tid), ___startTime, ___endTime, (label_s)); \
            } \
        } \
    }
    #define TIMELINE_END(tid, name) TIMELINE_END_C((tid), (name), true)
    #define TIMELINE_END_Llu(tid, name, label_lu) TIMELINE_END_C_Llu((tid), (name), true, (label_lu))
    #define TIMELINE_END_Ls(tid, name, label_s) TIMELINE_END_C_Ls((tid), (name), true, (label_s))
#else
    #define TIMELINE_BLIP(tid, name)
    #define TIMELINE_BLIP_Llu(tid, name, label_lu)
    #define TIMELINE_BLIP_Ls(tid, name, label_s)
    #define TIMELINE_START(tid)
    #define TIMELINE_START_C(tid, condition)
    #define TIMELINE_END_C(tid, name, condition)
    #define TIMELINE_END_C_Llu(tid, name, condition, label_lu)
    #define TIMELINE_END_C_Ls(tid, name, condition, label_s)
    #define TIMELINE_END(tid, name)
    #define TIMELINE_END_Llu(tid, name, label_lu)
    #define TIMELINE_END_Ls(tid, name, label_s)
#endif

#ifdef MEASURE_DURATION_STATS
    #define DURATION_START_C(tid, condition) \
        uint64_t ___startTime; \
        if ((condition)) { \
            ___startTime = get_server_clock(); \
        }
    #define DURATION_START(tid) DURATION_START_C((tid), true)
    #define DURATION_END_C(tid, stat_id, condition) { \
        if ((condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            GSTATS_ADD((tid), (stat_id), (___endTime - ___startTime)); \
        } \
    }
    #define DURATION_END(tid, stat_id) DURATION_END_C((tid), (stat_id), true)
#else
    #define DURATION_START_C(tid, condition)
    #define DURATION_START(tid)
    #define DURATION_END_C(tid, stat_id, condition)
    #define DURATION_END(tid, stat_id)
#endif

#ifdef PERIODIC_PT_THROUGHPUT_PRINT_EFFICIENT
    #define PERIODIC_START_C(tid, condition) \
        uint64_t ___startTime; \
        uint passed_s = 0; \
        if ((condition)) { \
            ___startTime = get_server_clock(); \
        }
    #define PERIODIC_START(tid) PERIODIC_START_C((tid), true)
    #define PERIODIC_END_C(tid, stat_id, condition) { \
        if ((condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            /* GSTATS_ADD((tid), (stat_id), (___endTime - ___startTime)); */ \
            printf("timeline_%s tid=%d start=%lu end=%lu\n", (stat_id), (tid), ___startTime, ___endTime); \
        } \
    }
    #define PERIODIC_END(tid, stat_id) PERIODIC_END_C((tid), (stat_id), true)
    #define PERIODIC_INTERIM(tid, interim_time, ts_stat_id, value_stat_id, stat_id) \
        auto ___interim_time = get_server_clock() /* interim_time */; \
        auto ___elapsed_duration_s = (___interim_time - ___startTime)/1000000000; \
        if ( ___elapsed_duration_s != (passed_s) ) \
        { \
            ++passed_s; \
            /* COUTATOMICTID("___elapsed_duration_s="<<___elapsed_duration_s<<" passed_s:"<<passed_s<<" pt_throughput="<<GSTATS_GET(tid, (num_operations))/(___elapsed_duration_s)<<std::endl); */ \
            /* commented for THREAD_DELAY to avoid printing COUTATOMICTID(" pt_throughput="<<GSTATS_GET(tid, (stat_id))/(___elapsed_duration_s)<<std::endl); */ \
            long val = GSTATS_GET(tid, (stat_id))/(___elapsed_duration_s); \
            GSTATS_APPEND(tid, ts_stat_id, ___interim_time); \
            GSTATS_APPEND(tid, value_stat_id, val); \
        }

#else
    #define PERIODIC_START_C(tid, condition)
    #define PERIODIC_START(tid)
    #define PERIODIC_END_C(tid, stat_id, condition)
    #define PERIODIC_END(tid, stat_id)
#endif //#ifdef PERIODIC_PT_THROUGHPUT_PRINT_EFFICIENT

/**
 * Wrapper to collect "BLIP" timeline stats instead of printing them. This is efficient because timeline printing effects experiment throughput a lot.
*/
#ifdef MEASURE_TIMELINE_GSTATS
    PAD;
    volatile bool ___timeline_gstats_use = 0;
    PAD;
    volatile static thread_local uint ___tg_collect_freq=1; //enables to decide the stat collection frequency at run time. Better to quickly see overhead with stat collection freq.
    PAD;
    #define ___MIN_INTERVAL_DURATION 0
    #define _STRINGIFY_VAR(var_name) #var_name 

    #define TIMELINE_GSTATS_BLIP_CW(tid, ts_stat_id, value_stat_id, val, condition) \
    do {\
        static thread_local unsigned long long int ___numop_since_tg_collect = 0; \
        if ((___timeline_gstats_use) && (++___numop_since_tg_collect%___tg_collect_freq == 0) && (condition)){ \
            auto time = get_server_clock(); \
            GSTATS_APPEND(tid, ts_stat_id, time); \
            GSTATS_APPEND(tid, value_stat_id, val); \
            /* printf("ts_stat_name=%s,___tg_collect_freq=%u, ___numop_since_tg_collect=%llu\n",_STRINGIFY_VAR(ts_stat_id), (___tg_collect_freq), (___numop_since_tg_collect));*/ \
        } \
    }while(0)

    #define TIMELINE_GSTATS_INTERVAL_START_CW(tid, condition) \
    uint64_t ___start_time; \
    do {\
        static thread_local unsigned long long int ___numop_since_tg_collect = 0; \
        if ((___timeline_gstats_use) && (++___numop_since_tg_collect%___tg_collect_freq == 0) && (condition)){ \
            ___start_time = get_server_clock(); \
/*             printf("%s, ___start_time=%lu, ___tg_collect_freq=%u, ___numop_since_tg_collect=%llu\n",_STRINGIFY_VAR(TIMELINE_GSTATS_INTERVAL_START), (___start_time), (___tg_collect_freq), (___numop_since_tg_collect));*/ \
        } \
    }while(0)

    /**
     * Make sure for interval events start and end time macros are paired properly within same scope.
     * ___numop_since_tg_collect is declared twice separately for start and end to avoid conflict with blip events.
    */
    #define TIMELINE_GSTATS_INTERVAL_END_CW(tid, start_ts_stat_id, end_ts_stat_id, value_stat_id, val, condition) \
    do {\
        static thread_local unsigned long long int ___numop_since_tg_collect = 0; \
        if ((___timeline_gstats_use) && (++___numop_since_tg_collect%___tg_collect_freq == 0) && (condition)){ \
            auto ___end_time = get_server_clock(); \
            GSTATS_APPEND(tid, start_ts_stat_id, ___start_time); \
            GSTATS_APPEND(tid, end_ts_stat_id, ___end_time); \
            GSTATS_APPEND(tid, value_stat_id, val); \
/*             printf("ts_stat_name=%s,___start_time=%lu,___end_time=%lu, ___tg_collect_freq=%u, ___numop_since_tg_collect=%llu\n",_STRINGIFY_VAR(end_ts_stat_id), (___start_time),(___end_time),(___tg_collect_freq), (___numop_since_tg_collect));*/ \
        } \
    }while(0)

    #define TIMELINE_GSTATS_BLIP_W(tid, ts_stat_id, value_stat_id, val) TIMELINE_GSTATS_BLIP_CW(tid, ts_stat_id, value_stat_id, val, true)
    #define TIMELINE_GSTATS_INTERVAL_START_W(tid) TIMELINE_GSTATS_INTERVAL_START_CW(tid, true) 
    #define TIMELINE_GSTATS_INTERVAL_END_W(tid, start_ts_stat_id, end_ts_stat_id, value_stat_id, val) TIMELINE_GSTATS_INTERVAL_END_CW(tid, start_ts_stat_id, end_ts_stat_id, value_stat_id, val, true)
#else
    #define TIMELINE_GSTATS_BLIP_W (tid, ts_stat_id, value_stat_id, val)
    #define TIMELINE_GSTATS_INTERVAL_START_W(tid)
    #define TIMELINE_GSTATS_INTERVAL_END_W(tid, start_ts_stat_id, end_ts_stat_id, value_stat_id, val)
    #define TIMELINE_GSTATS_INTERVAL_START_CW(tid, condition)
    #define TIMELINE_GSTATS_INTERVAL_END_CW(tid, start_ts_stat_id, end_ts_stat_id, value_stat_id, val, condition)
#endif //#ifdef MEASURE_TIMELINE_GSTATS


#endif /* SERVER_CLOCK_H */
