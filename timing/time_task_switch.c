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
#define MEASUREMENT_COUNT (50000U) /* number of iterations to run for the actual measurement */

/*******************************************************************************
 * Variables
 ******************************************************************************/

uint32_t temp_cyccnt_read = 0; /* ETX.h */

/* General t0, t1 measurement markers*/
static uint32_t test_start_time        = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t test_stop_time         = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t test_a_s1_time         = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t test_a_s3_time         = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t test_a_s1_time_min     = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t test_a_s1_time_max     = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t test_a_s1_time_outlier = 0U; /* time taken to run the benchmark loop, measured at runtime */
static uint32_t plus1                  = 0U;
static uint32_t plus2                  = 0U;
/* Possible overhead values measurement */
static uint32_t       test_dwta_ov = 0U; /* loop overhead in cycles, measured at runtime */
static uint32_t       test_loop_ov = 0U;
static volatile float scenario_1_a, scenario_3_a;

/*******************************************************************************
 * Code
 ******************************************************************************/

// /* Low Prio Task*/
// static void L_Task(ETX_t_TSK_HDL h_task_hdl, void *pv_task_data)
// {
//     DWT->CYCCNT = 0U;
//     __ISB();
//     __DSB();
//     test_start_time = DWT->CYCCNT; /* S1_A */
//     ETX_EventSet(H_TASK_HDL, 0x0001U);
//     (void) pv_task_data;
//     __NOP();
// }

// /* High Prio Task*/
// static void H_Task(ETX_t_TSK_HDL h_task_hdl, void *pv_task_data)
// {
//     test_stop_time = DWT->CYCCNT;                     /* S1_A */
//     if (ETX_EventGet(h_task_hdl, 0x0002U) == 0x0002U) /* S3_A */
//     {
//         return;
//     }
//     test_stop_time = test_stop_time - test_start_time - test_dwta_ov; /* S1_A */
//     test_a_s1_time += test_stop_time;                                 /* S1_A */
//     if (test_a_s1_time_min == 0U)
//     {
//         test_a_s1_time_min = test_stop_time;
//     }
//     if (test_stop_time > test_a_s1_time_max)
//     {
//         test_a_s1_time_max = test_stop_time;
//     }
//     if (test_stop_time < test_a_s1_time_min)
//     {
//         test_a_s1_time_min = test_stop_time;
//     }
//     if (test_stop_time != test_a_s1_time_min)
//     {
//         test_a_s1_time_outlier++;
//     }
//     (void) pv_task_data;
//     ETX_EventGet(h_task_hdl, 0x0001U);

//     /* Measure API Cost on Same Band i.o. to preserve ISR and Schedule cost*/
//     test_start_time = DWT->CYCCNT; /* S3_A */
//     ETX_EventSet(h_task_hdl, 0x0002U);
//     test_a_s3_time +=
//         DWT->CYCCNT - test_start_time - test_dwta_ov - MEASUREMENT_EVENT_SET_DETERMINED_EPILOGUE_O0; /* S3_A */
// }

/*!
 * @brief Main function
 */
int main(void)
{
    BENCHMARK_hardware_init();

    BENCHMARK_calc_overhead(MEASUREMENT_COUNT, &test_loop_ov, &test_dwta_ov);

    BENCHMARK_signal_measurement_start();

    // /* Create the blink task and request its first execution. */
    // if (ETX_TaskCreate(L_TASK_HDL, L_Task, (void *) 0) != IXX_TRUE)
    // {
    //     ETX_EXCEPTION_THROW();
    //     __BKPT(100);
    // }

    // if (ETX_TaskCreate(H_TASK_HDL, H_Task, (void *) 0) != IXX_TRUE)
    // {
    //     ETX_EXCEPTION_THROW();
    //     __BKPT(100);
    // }

    // for (uint32_t i = 0; i < MEASUREMENT_COUNT; i++)
    // {
    //     ETX_TaskActivate(L_TASK_HDL);
    //     ETX_Scheduler(0);
    // }

    // scenario_1_a = (float) test_a_s1_time / (float) MEASUREMENT_COUNT;
    // scenario_3_a = (float) test_a_s3_time / (float) MEASUREMENT_COUNT;
    while (1)
    {
        BENCHMARK_signal_measurement_stop();
    }

    return 0;
}
