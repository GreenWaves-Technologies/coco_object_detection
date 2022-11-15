
/*
 * Copyright (C) 2017 GreenWaves Technologies
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms
 * of the BSD license.  See the LICENSE file for details.
 *
 */


/* Autotiler includes. */
#include "ssd_mobilenet.h"
#include "ssd_mobilenetKernels.h"
#include "modelInfos.h"
#include "gaplib/ImgIO.h"
#include "bsp/bsp.h"
#include "bsp/ram.h"
#include "coco_labels.h"
#include "measurments_utils.h"

#define WIDTH 300
#define HEIGHT 300

struct pi_device DefaultRam;
AT_HYPERFLASH_EXT_ADDR_TYPE ssd_mobilenet_L3_Flash = 0;
AT_HYPERFLASH_EXT_ADDR_TYPE ssd_mobilenet_L3_PrivilegedFlash = 0;

/* Global Buffers and Variables */
static L2_MEM short int OutputBoxes[40];
static L2_MEM signed char OutputClasses[10];
static L2_MEM signed char OutputScores[10];

#define __XSTR(__s) __STR(__s)
#define __STR(__s) #__s 
char *ImageName = __XSTR(AT_IMAGE);

static void open_ram(struct pi_device *device)
{
    /* Init & open ram. */
    struct pi_default_ram_conf default_conf;
    pi_default_ram_conf_init(&default_conf);
    pi_open_from_conf(device, (void *) &default_conf);
    if (pi_ram_open(device))
    {
        printf("Error ram open !\n");
        pmsis_exit(-3);
    }
}

static void cluster()
{
    #ifdef PERF
    gap_cl_starttimer();
    gap_cl_resethwtimer();
    #endif

GPIO_HIGH();
    ssd_mobilenetCNN(OutputBoxes, OutputClasses, OutputScores);
GPIO_LOW();
}

int test_ssd_mobilenet(void)
{
    OPEN_GPIO_MEAS();
    GPIO_LOW();

    printf("Entering main controller\n");

    /* Frequency Settings: defined in the Makefile */
    int cur_fc_freq = pi_freq_set(PI_FREQ_DOMAIN_FC, FREQ_FC*1000*1000);
    int cur_cl_freq = pi_freq_set(PI_FREQ_DOMAIN_CL, FREQ_CL*1000*1000);
    int cur_pe_freq = pi_freq_set(PI_FREQ_DOMAIN_PERIPH, FREQ_PE*1000*1000);
    if (cur_fc_freq == -1 || cur_cl_freq == -1 || cur_pe_freq == -1)
    {
        printf("Error changing frequency !\nTest failed...\n");
        pmsis_exit(-4);
    }
    printf("FC Frequency as %d Hz, CL Frequency = %d Hz, PERIIPH Frequency = %d Hz\n", 
            pi_freq_get(PI_FREQ_DOMAIN_FC), pi_freq_get(PI_FREQ_DOMAIN_CL), pi_freq_get(PI_FREQ_DOMAIN_PERIPH));

    /* Configure And open cluster. */
    struct pi_device cluster_dev;
    struct pi_cluster_conf cl_conf;
    pi_cluster_conf_init(&cl_conf);
    cl_conf.id = 0;
    cl_conf.cc_stack_size = STACK_SIZE;
    pi_open_from_conf(&cluster_dev, (void *) &cl_conf);
    if (pi_cluster_open(&cluster_dev))
    {
        printf("Cluster open failed !\n");
        pmsis_exit(-4);
    }

    /* Open Ram */
    printf("opening ram...\n");
    open_ram(&DefaultRam);

    // IMPORTANT - MUST BE CALLED AFTER THE CLUSTER IS SWITCHED ON!!!!
    printf("Constructor\n");
    int ConstructorErr = ssd_mobilenetCNN_Construct();
    if (ConstructorErr)
    {
        printf("Graph constructor exited with error: %d\n(check the generated file ssd_mobilenetKernels.c to see which memory have failed to be allocated)\n", ConstructorErr);
        pmsis_exit(-6);
    }

    if (ReadImageFromFile(ImageName, WIDTH, HEIGHT, 3, Input_1, WIDTH * HEIGHT * 3, IMGIO_OUTPUT_CHAR, 0))
    {
        printf("Failed to load image %s\n", ImageName);
        pmsis_exit(-6);
    }

    struct pi_cluster_task task;
    pi_cluster_task(&task, (void (*)(void *))cluster, NULL);
    pi_cluster_task_stacks(&task, NULL, SLAVE_STACK_SIZE);
    pi_cluster_send_task_to_cl(&cluster_dev, &task);

    for(int i=0; i<10; i++) {
        printf("BBox[%d]: (%5d %5d %5d %5d) Class: %2d Confidence: %3d\n",
            i, OutputBoxes[4*i], OutputBoxes[4*i+1], OutputBoxes[4*i+2], OutputBoxes[4*i+3], OutputClasses[i], OutputScores[i]);
    }
#ifdef PERF
    unsigned int TotalCycles = 0, TotalOper = 0;
    printf("\n");
    for (unsigned int i=0; i<(sizeof(AT_GraphPerf)/sizeof(unsigned int)); i++) {
        TotalCycles += AT_GraphPerf[i]; TotalOper += AT_GraphOperInfosNames[i];
    }
    for (unsigned int i=0; i<(sizeof(AT_GraphPerf)/sizeof(unsigned int)); i++) {
        printf("%45s: Cycles: %12u, Cyc%%: %5.1f%%, Operations: %12u, Op%%: %5.1f%%, Operations/Cycle: %f\n", AT_GraphNodeNames[i], AT_GraphPerf[i], 100*((float) (AT_GraphPerf[i]) / TotalCycles), AT_GraphOperInfosNames[i], 100*((float) (AT_GraphOperInfosNames[i]) / TotalOper), ((float) AT_GraphOperInfosNames[i])/ AT_GraphPerf[i]);
    }
  printf("\n");
    printf("%45s: Cycles: %12u, Cyc%%: 100.0%%, Operations: %12u, Op%%: 100.0%%, Operations/Cycle: %f\n", "Total", TotalCycles, TotalOper, ((float) TotalOper)/ TotalCycles);
printf("\n");
#endif

    ssd_mobilenetCNN_Destruct();

    printf("Ended\n");
    pmsis_exit(0);
    return 0;
}

int main(int argc, char *argv[])
{
    printf("\n\n\t *** NNTOOL ssd_mobilenet Example ***\n\n");
    test_ssd_mobilenet();
    return 0;
}
