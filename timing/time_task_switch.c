/*
 * Copyright 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * ETX benchmark bring-up: a single periodic ETX task that toggles the board LED.
 * Demonstrates the kernel scheduling a task off its own re-armed task timer.
 */

#include <stdio.h>
#include <zephyr/kernel.h>

#include "benchmark_common.h"
#include "benchmark_tools.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#define THREAD_STACK_SIZE 512
#define IDLE_THREAD_PRIORITY 3
#define LOW_PRIO_THREAD_PRIORITY 2
#define HIGH_PRIO_THREAD_PRIORITY 1

#define MEASUREMENT_COUNT (50000U) /* number of iterations to run for the actual measurement */

/* Signals multiplexed onto the single test_event object. */
#define SCENARIO_1_EVENT_MASK 0x0001U      /* L_Task -> H_Task: S1_A stop marker, the measured switch */
#define SCENARIO_3_EVENT_MASK 0x0002U      /* never posted: S3_A probes wait API cost */
#define SIGNALIZE_YIELD_EVENT_MASK 0x0004U /* H_Task -> I_Task: iteration complete, re-arm L_Task */
#define START_EVENT_MASK 0x0008U           /* H_Task -> L_Task: measurement may begin */

/*******************************************************************************
 * Variables
 ******************************************************************************/

uint32_t temp_cyccnt_read = 0; /* ETX.h */

/* General t0, t1 measurement markers*/
static volatile uint32_t test_start_time        = 0U; /* time taken to run the benchmark loop, measured at runtime */
static volatile uint32_t test_stop_time         = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t          test_a_s1_time         = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t          test_a_s3_time         = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t          test_a_s1_time_min     = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t          test_a_s1_time_max     = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t          test_a_s1_time_outlier = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t          plus1                  = 0U;
static uint32_t          plus2                  = 0U;
/* Possible overhead values measurement */
static uint32_t       test_dwta_ov = 0U; /* loop overhead in cycles, measured at runtime */
static uint32_t       test_loop_ov = 0U;
static volatile float scenario_1_a, scenario_3_a;

struct k_event test_event;

K_THREAD_STACK_DEFINE(idle_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(low_prio_stack, THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(high_prio_stack, THREAD_STACK_SIZE);

static struct k_thread idle_thread;
static struct k_thread low_prio_thread;
static struct k_thread high_prio_thread;

/*******************************************************************************
 * Code
 ******************************************************************************/

// /* Low Prio Task*/
static void L_Task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    k_event_wait_safe(&test_event, START_EVENT_MASK, false, K_FOREVER); /* Started? */
    while (1)
    {
        BENCHMARK_reset_counter();
        BENCHMARK_GET_START_TIME(test_start_time);
        k_event_post(&test_event, SCENARIO_1_EVENT_MASK);
        k_thread_suspend(k_current_get());
    }
}

// /* High Prio Task*/
static void H_Task(void *p1, void *p2, void *p3)
{
    k_event_post(&test_event, START_EVENT_MASK); /*signalize start*/
    while (1)
    {
        k_event_wait_safe(&test_event, SCENARIO_1_EVENT_MASK, false, K_FOREVER); /* S1_A */
        BENCHMARK_GET_STOP_TIME(test_stop_time);                                 /* S1_A */
        test_stop_time = test_stop_time - test_start_time - test_dwta_ov;        /* S1_A */
        test_a_s1_time += test_stop_time;                                        /* S1_A */
        if (test_a_s1_time_min == 0U)
        {
            test_a_s1_time_min = test_stop_time;
        }
        else if (test_stop_time != test_a_s1_time_min)
        {
            test_a_s1_time_outlier++;
            // BENCHMARK_signal_jitter_detected(NULL, 0);
        }
        if (test_stop_time > test_a_s1_time_max)
        {
            test_a_s1_time_max = test_stop_time;
        }
        if (test_stop_time < test_a_s1_time_min)
        {
            test_a_s1_time_min = test_stop_time;
        }
        ARG_UNUSED(p1);
        ARG_UNUSED(p2);
        ARG_UNUSED(p3);
        BENCHMARK_reset_counter();
        /* Measure API Cost on Same Band i.o. to preserve ISR and Schedule cost*/
        BENCHMARK_GET_START_TIME(test_start_time);
        k_event_wait_safe(&test_event, SCENARIO_3_EVENT_MASK, false, K_NO_WAIT);
        BENCHMARK_GET_STOP_TIME(test_stop_time);                           /* S3_A */
        test_a_s3_time += test_stop_time - test_start_time - test_dwta_ov; /* S3_A */
        k_event_post(&test_event, SIGNALIZE_YIELD_EVENT_MASK);             /* YIELD TO I_Task */
    }
}

static void I_Task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    k_event_init(&test_event);

    k_thread_create(&high_prio_thread, high_prio_stack, THREAD_STACK_SIZE, H_Task, NULL, NULL, NULL,
                    HIGH_PRIO_THREAD_PRIORITY, 0, K_NO_WAIT);

    k_thread_create(&low_prio_thread, low_prio_stack, THREAD_STACK_SIZE, L_Task, NULL, NULL, NULL,
                    LOW_PRIO_THREAD_PRIORITY, 0, K_NO_WAIT);

    for (uint32_t i = 0; i < MEASUREMENT_COUNT - 1; i++)
    {
        k_event_wait_safe(&test_event, SIGNALIZE_YIELD_EVENT_MASK, false, K_FOREVER);
        k_thread_resume(&low_prio_thread);
    }

    scenario_1_a = (float) test_a_s1_time / (float) MEASUREMENT_COUNT;
    scenario_3_a = (float) test_a_s3_time / (float) MEASUREMENT_COUNT;

    while (1)
    {
        BENCHMARK_signal_measurement_stop();
    }
}

/*!
 * @brief Main function
 */
int main(void)
{
    BENCHMARK_hardware_init();

    /* SystemInit enabled the LPCAC before main and Zephyr never turns it off: disable and clear it. */
    SYSCON->LPCAC_CTRL |= (SYSCON_LPCAC_CTRL_DIS_LPCAC_MASK | SYSCON_LPCAC_CTRL_CLR_LPCAC_MASK);

    Benchmark_disable_sys_tick();

    BENCHMARK_calc_overhead(MEASUREMENT_COUNT, &test_loop_ov, &test_dwta_ov);

    // Benchmark_enable_sys_tick();

    BENCHMARK_signal_measurement_start();

    k_thread_create(&idle_thread, idle_stack, THREAD_STACK_SIZE, I_Task, NULL, NULL, NULL, IDLE_THREAD_PRIORITY, 0,
                    K_NO_WAIT);

    return 0;
}
