
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
#include "ResizeBasicKernels.h"
#include "modelInfos.h"
#include "gaplib/ImgIO.h"
#include "bsp/bsp.h"
#include "bsp/ram.h"
#include "bsp/display/ili9341.h"
#include "coco_labels.h"

#define WIDTH 300
#define HEIGHT 300

#define CAMERA_WIDTH 320
#define CAMERA_HEIGHT 240
#define CAMERA_MINDIM 240

#ifdef DISPLAY
#define MAX_DISPLAY_WIDTH 320
#define MAX_DISPLAY_HEIGHT 240
#define SCORE_THRESHOLD 50
#else
#define MAX_DISPLAY_WIDTH WIDTH
#define MAX_DISPLAY_HEIGHT HEIGHT
#define SCORE_THRESHOLD 0
#endif

struct pi_device DefaultRam;
AT_HYPERFLASH_EXT_ADDR_TYPE ssd_mobilenet_L3_Flash = 0;
AT_HYPERFLASH_EXT_ADDR_TYPE ssd_mobilenet_L3_PrivilegedFlash = 0;

/* Global Buffers and Variables */
static L2_MEM uint32_t RamImageBuffer;
static L2_MEM short int OutputBoxes[40];
static L2_MEM signed char OutputClasses[10];
static L2_MEM signed char OutputScores[10];
static L2_MEM char labels[5][50];
static L2_MEM unsigned short colors[5] = {0x001F, 0x07E0, 0x07FF, 0xF800, 0xFFE0};

struct pi_device ili;
static pi_buffer_t buffer;

#ifdef FROM_FILE
#define __XSTR(__s) __STR(__s)
#define __STR(__s) #__s 
char *ImageName = __XSTR(AT_IMAGE);
#else
struct pi_device camera;
static void open_camera(struct pi_device *device)
{
    struct pi_ov9281_conf cam_conf;
    pi_ov9281_conf_init(&cam_conf);
    cam_conf.format=PI_CAMERA_QVGA;
    pi_open_from_conf(device, &cam_conf);
    if (pi_camera_open(device)){
        printf("Failed open Camera\n");
        pmsis_exit(-1);
    }
}
#endif

static void Resize(KerResizeBilinear_ArgT *KerArg)
{
    AT_FORK(gap_ncore(), (void *) KerResizeBilinear, (void *) KerArg);
}

#ifdef DISPLAY
static void open_display(struct pi_device *device)
{
    struct pi_ili9341_conf ili_conf;
    pi_ili9341_conf_init(&ili_conf);
    pi_open_from_conf(device, &ili_conf);
    if (pi_display_open(device))
    {
        printf("Failed to open display\n");
        pmsis_exit(-1);
    }
    //orientation at 270 degrees
    if (pi_display_ioctl(device, PI_ILI_IOCTL_ORIENTATION, (void *)PI_ILI_ORIENTATION_270))
    {
        printf("Failed to set orientation display\n");
        pmsis_exit(-1);
    }
    setCursor(device, 0, 0);
}
#endif

static void open_and_alloc_ram(struct pi_device *device, uint32_t Buffer, uint32_t size)
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

    if (pi_ram_alloc(device, &Buffer, (uint32_t) size))
    {
        printf("Ram malloc failed !\n");
        pmsis_exit(-4);
    }
}

static inline void draw_text(struct pi_device *display, const char *str, unsigned posX, unsigned posY, unsigned fontsize, unsigned int color)
{
    //writeFillRect(display, 0, 340, posX, fontsize*10, 0xFFFF);
    setCursor(display, posX, posY);
    setTextColor(display, color);
    writeText(display, str, fontsize);
    setCursor(display, 0, 0);
}

static inline void FromGrayToRGB565(unsigned char *image, int width, int height) {
    for (int h=(height-1); h>=0; h--) {
        for (int w=(width-1); w>=0; w--) {
            unsigned short pixel = (unsigned short) image[h*width + w];
            ((unsigned short *) image)[h*width + w] = (((pixel&0xf8)<<8)|((pixel&0xfc)<<3)|((pixel&0xf8)>>3));
        }
    }
}

static inline void WriteRectOnImage(unsigned short *image,  int x,  int y,  int w,  int h, unsigned short color) {
    int line_width = 2;
    //int color = 0xF800;
    /* top */
    for (int j=y; (j<MAX_DISPLAY_HEIGHT) && (j<(y+line_width)); j++) {
        for (int i=x; (i<MAX_DISPLAY_WIDTH) && (i<(x+w)); i++) {
            image[j*WIDTH + i] = color;
        }
    }
    /* bottom */
    for (int j=y+h; (j<MAX_DISPLAY_HEIGHT) && (j<(y+h+line_width)); j++) {
        for (int i=x; (i<MAX_DISPLAY_WIDTH) && (i<(x+w+line_width)); i++) {
            image[j*WIDTH + i] = color;
        }
    }
    /* left */
    for (int j=y; (j<MAX_DISPLAY_HEIGHT) && (j<(y+h)); j++) {
        for (int i=x; (i<MAX_DISPLAY_WIDTH) && (i<(x+line_width)); i++) {
            image[j*WIDTH + i] = color;
        }
    }
    /* right */
    for (int j=y; (j<MAX_DISPLAY_HEIGHT) && (j<(y+h+line_width)); j++) {
        for (int i=x+w; (i<MAX_DISPLAY_WIDTH) && (i<(x+w+line_width)); i++) {
            image[j*WIDTH + i] = color;
        }
    }
}

static inline void WriteFillRectOnImage(unsigned short *image,  int x,  int y,  int w,  int h, unsigned short color) {
    for (int j=y; j<(y+h); j++) {
        for (int i=x; i<(x+w); i++) {
            image[j*WIDTH + i] = color;
        }
    }
}

static void cluster()
{
    #ifdef PERF
    gap_cl_starttimer();
    gap_cl_resethwtimer();
    #endif

    int cycles = gap_cl_readhwtimer();
    ssd_mobilenetCNN(OutputBoxes, OutputClasses, OutputScores);
    cycles = gap_cl_readhwtimer() - cycles;
    //printf("Executed in %d Cycles (%.2f inf/sec)\n", cycles, cycles / (FREQ_CL*1000*1000));
}

int test_ssd_mobilenet(void)
{
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

    /* Open display */
    printf("opening and alloc ram buffer...\n");
    open_and_alloc_ram(&DefaultRam, RamImageBuffer, WIDTH*HEIGHT);

#ifdef DISPLAY
    /* Open display */
    printf("opening display...\n");
    // BLUE 0x001F DARKCYAN 0x03EF NAVY 0x000F WHITE 0xFFFF CYAN 0x07FF RED 0xF800
    open_display(&ili);
#endif


#ifndef FROM_FILE
    /* Open camera */
    printf("opening camera...\n");
    open_camera(&camera);
#endif

    // IMPORTANT - MUST BE CALLED AFTER THE CLUSTER IS SWITCHED ON!!!!
    printf("Constructor\n");
    int ConstructorErr = ssd_mobilenetCNN_Construct();
    if (ConstructorErr)
    {
        printf("Graph constructor exited with error: %d\n(check the generated file ssd_mobilenetKernels.c to see which memory have failed to be allocated)\n", ConstructorErr);
        pmsis_exit(-6);
    }
    unsigned char *L2ImageBuffer = Input_1 + WIDTH*HEIGHT*3 - CAMERA_WIDTH*CAMERA_HEIGHT;

    pi_buffer_init(&buffer, PI_BUFFER_TYPE_L2, Input_1);
    pi_buffer_set_stride(&buffer, 0);
    pi_buffer_set_format(&buffer, 300, 240, 1, PI_BUFFER_FORMAT_RGB565);
    pi_buffer_set_data(&buffer, Input_1);

    gap_fc_starttimer();
    gap_fc_resethwtimer();
    while(1) {

        int start = pi_time_get_us();
        #ifdef FROM_FILE
            if (ReadImageFromFile(ImageName, CAMERA_WIDTH, CAMERA_HEIGHT, 1, L2ImageBuffer, CAMERA_WIDTH * CAMERA_HEIGHT * 1, IMGIO_OUTPUT_CHAR, 0))
            {
                printf("Failed to load image %s\n", ImageName);
                pmsis_exit(-6);
            }
        #else
            pi_evt_t task_camera;
            pi_camera_capture_async(&camera, L2ImageBuffer, CAMERA_WIDTH*CAMERA_HEIGHT, pi_evt_sig_init(&task_camera));
            pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
            pi_evt_wait(&task_camera);
            pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
        #endif
        int get_image = pi_time_get_us() - start;

        start = pi_time_get_us();
        /* Image Cropping to [ CAMERA_MINDIM x CAMERA_MINDIM ] */
        int idx=0;
        for(int i =0;i<CAMERA_HEIGHT;i++){
            for(int j=0;j<CAMERA_WIDTH;j++){
                if (i<CAMERA_MINDIM && j<CAMERA_MINDIM){
                    L2ImageBuffer[idx] = L2ImageBuffer[i*CAMERA_WIDTH+j];
                    idx++;
                }
            }
        }
        // WriteImageToFile("../OutCropped.pgm", CAMERA_MINDIM, CAMERA_MINDIM, sizeof(uint8_t), L2ImageBuffer, GRAY_SCALE_IO);

        /* Resize to [ HEIGHT x WIDTH ] */
        struct pi_cluster_task task_resize;
        KerResizeBilinear_ArgT ResizeArg;
        ResizeArg.In             = L2ImageBuffer;
        ResizeArg.Win            = CAMERA_MINDIM;
        ResizeArg.Hin            = CAMERA_MINDIM;
        ResizeArg.Out            = Input_1;
        ResizeArg.Wout           = WIDTH;
        ResizeArg.Hout           = HEIGHT;
        ResizeArg.HTileOut       = HEIGHT;
        ResizeArg.FirstLineIndex = 0;
        pi_cluster_task(&task_resize, (void (*)(void *))Resize, &ResizeArg);
        pi_cluster_task_stacks(&task_resize, NULL, SLAVE_STACK_SIZE);
        pi_cluster_send_task_to_cl(&cluster_dev, &task_resize);
        int crop_and_resize_us = pi_time_get_us() - start;

        start = pi_time_get_us();
        pi_ram_copy(&DefaultRam, RamImageBuffer, (void *) Input_1, HEIGHT*WIDTH, 0);
        for (int h=HEIGHT-1; h>=0; h--) {
            for (int w=WIDTH-1; w>=0; w--) {
                Input_1[h*WIDTH*3 + w*3 + 0] = Input_1[h*WIDTH + w];
                Input_1[h*WIDTH*3 + w*3 + 1] = Input_1[h*WIDTH + w];
                Input_1[h*WIDTH*3 + w*3 + 2] = Input_1[h*WIDTH + w];
            }
        }
        int ram_copy_us = pi_time_get_us() - start;

        start = pi_time_get_us();
        struct pi_cluster_task task;
        pi_cluster_task(&task, (void (*)(void *))cluster, NULL);
        pi_cluster_task_stacks(&task, NULL, SLAVE_STACK_SIZE);
        pi_cluster_send_task_to_cl(&cluster_dev, &task);
        int nn_us = pi_time_get_us() - start;

        start = pi_time_get_us();
        pi_ram_copy(&DefaultRam, RamImageBuffer, (void *) Input_1, HEIGHT*WIDTH, 1);
        FromGrayToRGB565(Input_1, WIDTH, HEIGHT);

        /* Box for fps text */
        #ifdef DISPLAY
        WriteFillRectOnImage((unsigned short *) Input_1, 0, 0, 220, 25, 0xFFFF);
        #endif

        unsigned short class_colors[5] = {0, 0, 0, 0, 0};
        int color_idx = 0;
        for (int i=0; i<5; i++) {
            if ((OutputScores[i]>SCORE_THRESHOLD) && (OutputClasses[i]>0) && (OutputClasses[i]<80)) {
                int pred_class = OutputClasses[i];
                /* If classes already in the list use the same color */
                int is_already = 0;
                for (int j=0; j<i; j++) {
                    if (OutputClasses[j] == pred_class) {
                        is_already = 1;
                        break;
                    }
                }
                if (!is_already){
                    #ifdef DISPLAY
                    WriteFillRectOnImage((unsigned short *) Input_1, 0, 25+25*color_idx, 100, 25, 0xFFFF);
                    #endif
                    // sprintf(labels[color_idx], "%d", pred_class);
                    sprintf(labels[color_idx], "%s", coco_labels[pred_class-1]);
                    color_idx++;
                }

                class_colors[i] = colors[color_idx];

                int box_y_min = Max(0,      (int)(FIX2FP(OutputBoxes[i*4+0]*ssd_mobilenet_Output_1_OUT_QSCALE,ssd_mobilenet_Output_1_OUT_QNORM)*HEIGHT));
                int box_x_min = Max(0,      (int)(FIX2FP(OutputBoxes[i*4+1]*ssd_mobilenet_Output_1_OUT_QSCALE,ssd_mobilenet_Output_1_OUT_QNORM)*WIDTH));
                int box_y_max = Min(HEIGHT, (int)(FIX2FP(OutputBoxes[i*4+2]*ssd_mobilenet_Output_1_OUT_QSCALE,ssd_mobilenet_Output_1_OUT_QNORM)*HEIGHT));
                int box_x_max = Min(WIDTH,  (int)(FIX2FP(OutputBoxes[i*4+3]*ssd_mobilenet_Output_1_OUT_QSCALE,ssd_mobilenet_Output_1_OUT_QNORM)*WIDTH));
                int box_h = box_y_max - box_y_min;
                int box_w = box_x_max - box_x_min;

                WriteRectOnImage((unsigned short *) Input_1, box_x_min, box_y_min, box_w, box_h, class_colors[i]);
                // printf("BOX[%d] (x, y, w, h): (%d, %d, %d, %d) CLASS@SCORE: %d@%f\n", i, box_x_min, box_y_min, box_w, box_h, OutputClasses[i], FIX2FP(OutputScores[i],7));
            }
        }
#ifdef DISPLAY
        pi_display_write(&ili, &buffer, 0, 0, WIDTH, 240);
        for (int i=0; i<color_idx; i++) draw_text(&ili, labels[i], 5, 25+25*i, 2, class_colors[i]);
#else
        WriteImageToFile("../OutputImage.pgm", WIDTH, HEIGHT, 3, Input_1, RGB565_IO);
        for (int i=0; i<color_idx; i++) printf("%s\n", labels[i]);
#endif
        int display_us = pi_time_get_us() - start;

        /* Write Timings */
        char timing[50];
        sprintf(timing, "NN@%.2ffps (%.2f)", 1000000 / ((float) nn_us), 1000000 / ((float) (get_image+crop_and_resize_us+ram_copy_us+nn_us+display_us)));
#ifdef DISPLAY
        draw_text(&ili, timing, 5, 5, 2, 0xF800);
#else
        printf("get_image: %d crop_and_resize: %d ram: %d nn: %d display: %d - %s\n", get_image, crop_and_resize_us, ram_copy_us, nn_us, display_us, timing);
#endif
    }

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
