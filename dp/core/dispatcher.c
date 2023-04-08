/*
 * Copyright 2018-19 Board of Trustees of Stanford University
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * dispatcher.c - dispatcher core functionality
 *
 * A single core is responsible for receiving network packets from the network
 * core and dispatching these packets or contexts to the worker cores.
 */

#include <stdio.h>
#include <ix/cfg.h>
#include <ix/context.h>
#include <ix/dispatch.h>

#include <ix/networker.h>
#include <net/ip.h>
#include <net/udp.h>

#include "helpers.h"
#include "benchmark.h"

// ---- Added for tests ----
extern volatile bool TEST_STARTED;
extern volatile bool IS_FIRST_PACKET;
extern volatile bool INIT_FINISHED;
void print_stats(void);

volatile uint64_t TEST_START_TIME;
volatile uint64_t TEST_END_TIME;
volatile uint64_t TEST_RCVD_DB_GET;
volatile uint64_t TEST_RCVD_DB_ITERATOR;
volatile uint64_t TEST_RCVD_DB_PUT;
volatile uint64_t TEST_RCVD_DB_DELETE;
volatile uint64_t TEST_RCVD_DB_SEEK;
volatile uint64_t TEST_TOTAL_PACKETS_COUNTER = 0; 
volatile bool 	 TEST_FINISHED = false;
uint64_t dispatched_pkts = 0;

extern void dune_apic_send_posted_ipi(uint8_t vector, uint32_t dest_core);
extern void yield_handler(void);

#define PREEMPT_VECTOR 0xf2
#define PREEMPTION_DELAY 2000

static void timestamp_init(int num_workers)
{
        int i;
        for (i = 0; i < num_workers; i++)
                timestamps[i] = MAX_UINT64;
}

static void preempt_check_init(int num_workers)
{
        int i;
        for (i = 0; i < num_workers; i++)
                preempt_check[i] = false;
}

static inline void handle_finished(int i)
{
        if (worker_responses[i].req == NULL)
                log_warn("No mbuf was returned from worker\n");
        context_free(worker_responses[i].rnbl);
        request_enqueue(&frqueue, (struct request *) worker_responses[i].req);
        preempt_check[i] = false;
        worker_responses[i].flag = PROCESSED;
}

static inline void handle_preempted(int i)
{
        void * rnbl;
        struct request * req;
        uint8_t type, category;
        uint64_t timestamp;

        rnbl = worker_responses[i].rnbl;
        req = worker_responses[i].req;
        category = worker_responses[i].category;
        type = worker_responses[i].type;
        timestamp = worker_responses[i].timestamp;
        tskq_enqueue_tail(&tskq, rnbl, req, type, category, timestamp);
        preempt_check[i] = false;
        worker_responses[i].flag = PROCESSED;
}

static inline void dispatch_request(int i, uint64_t cur_time)
{
        void * rnbl;
        struct request * req;
        uint8_t type, category;
        uint64_t timestamp;

        if(tskq_dequeue(&tskq, &rnbl, &req, &type,
                              &category, &timestamp))
                return;
        worker_responses[i].flag = RUNNING;
        dispatcher_requests[i].rnbl = rnbl;
        dispatcher_requests[i].req = req;
        dispatcher_requests[i].type = type;
        dispatcher_requests[i].category = category;
        dispatcher_requests[i].timestamp = timestamp;
        timestamps[i] = cur_time;
        preempt_check[i] = true;
        dispatcher_requests[i].flag = ACTIVE;
}

static inline void preempt_worker(uint8_t i, uint64_t cur_time)
{
	if (preempt_check[i] && (((cur_time - timestamps[i]) / CPU_FREQ_GHZ) > PREEMPTION_DELAY))
	{
		// Avoid preempting more times.
		preempt_check[i] = false;
		dune_apic_send_posted_ipi(PREEMPT_VECTOR, CFG.cpu[i + 2]);
	}
}

static inline void handle_worker(int i, uint64_t cur_time)
{
        if (worker_responses[i].flag != RUNNING) {
                if (worker_responses[i].flag == FINISHED) {
                        handle_finished(i);
                } else if (worker_responses[i].flag == PREEMPTED) {
                        handle_preempted(i);
                }
                dispatch_request(i, cur_time);
        } else {
			#if (SCHEDULE_METHOD == METHOD_PI)
                preempt_worker(i, cur_time);
			#endif
		}
}

static inline void handle_networker(uint64_t cur_time)
{
	int i, ret;
	uint8_t type;
	ucontext_t *cont;

	if (networker_pointers.cnt != 0)
	{
		for (i = 0; i < networker_pointers.cnt; i++)
		{
			if(unlikely(networker_pointers.reqs[i] == NULL))
			{
				continue;
			}
			dispatched_pkts++;
			ret = context_alloc(&cont);
			if (unlikely(ret))
			{
				log_warn("Cannot allocate context\n");
                                request_enqueue(&frqueue, networker_pointers.reqs[i]);
                                continue;
			}

                        type = networker_pointers.types[i];
                        tskq_enqueue_tail(&tskq, cont,
                                          networker_pointers.reqs[i],
                                          type, PACKET, cur_time);
                }

                for (i = 0; i < ETH_RX_MAX_BATCH; i++) {
                        struct request * req = request_dequeue(&frqueue);
                        if (!req)
                                break;
                        networker_pointers.reqs[i] = req;
			networker_pointers.free_cnt++;
		}
		networker_pointers.cnt = 0;
	}
}


/**
 * do_dispatching - implements dispatcher core's main loop
 */
void do_dispatching(int num_cpus)
{
	uint8_t i;
	uint64_t cur_time;

	while (!INIT_FINISHED);
	
    preempt_check_init(num_cpus - 2);
    timestamp_init(num_cpus - 2);
	bool flag = true;

	log_info("Beginning dispatching\n");
	
	while (1)
	{
		if (flag && TEST_STARTED && IS_FIRST_PACKET && (TEST_FINISHED || ((get_us() - TEST_START_TIME) > BENCHMARK_DURATION_US )))
		{
			TEST_END_TIME = get_us();
			log_info("\n\n ----------- Benchmark FINISHED ----------- \n");
			log_info("Benchmark - Total number of packets %d \n", TEST_TOTAL_PACKETS_COUNTER);
			log_info("Benchmark - %d DB_GET, %d DB_ITERATOR, %d DB_PUT, %d DB_DELETE, %d DB_SEEK\n", TEST_RCVD_DB_GET, TEST_RCVD_DB_ITERATOR, TEST_RCVD_DB_PUT, TEST_RCVD_DB_DELETE, TEST_RCVD_DB_SEEK);
			log_info("Benchmark - Time elapsed (us): %llu\n",  TEST_END_TIME- TEST_START_TIME);
			uint64_t rate =  dispatched_pkts*1000/(TEST_END_TIME- TEST_START_TIME);
			log_info("Dispatched pkts, rate: %llu : %llu KRps\n", dispatched_pkts,rate);
			print_stats();
			log_info("Dispatcher exiting\n");
			flag = false;
			break;
		}

		cur_time = rdtsc();
		for (i = 0; i < num_cpus - 2; i++){
			handle_worker(i, cur_time);
		}
		handle_networker(cur_time);
	}
}
