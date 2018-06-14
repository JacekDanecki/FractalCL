/*
    Copyright (C) 2018  Jacek Danecki <jacek.m.danecki@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "fractal_ocl.h"
#include "gui.h"

#include "kernels/dragon.cl"
#include "kernels/julia.cl"
#include "kernels/julia3.cl"
#include "kernels/julia_full.cl"
#include "kernels/mandelbrot.cl"

unsigned int* colors;
void* cpu_pixels;

volatile int tasks_finished;

pthread_cond_t cond_fin;
pthread_mutex_t lock_fin;

FP_TYPE zx = 1.0, zy = 1.0; // zoom x, y
FP_TYPE zoom;
FP_TYPE dx, dy;               // shift left/right, down/up
FP_TYPE szx = 1.0, szy = 1.0; // scale x and y
FP_TYPE mx, my;               // mouse coordinates between [ofs_lx..ofs_rx, ofs_ty..ofs_by]

int mm = 16;

#if 0
FP_TYPE ofs_lx = -0.7402f;//-1.5f; //0.094; //-1.5f;
FP_TYPE ofs_rx = -0.7164f; //-0.716403707741993;//1.5f; //0.096; //1.5f;
FP_TYPE ofs_ty = 0.2205f;//0.283962759342063; //1.5f; //0.504f; //1.5f;
FP_TYPE ofs_by = 0.1690f;//0.283962759341958; //-1.5f; //0.503f; //-1.5f;
#else
#define OFS_LX -1.5f
#define OFS_RX 1.5f

FP_TYPE ofs_lx = OFS_LX;
FP_TYPE ofs_rx = OFS_RX;
FP_TYPE ofs_ty = 1.5f;
FP_TYPE ofs_by = -1.5f;
#endif
FP_TYPE er = 4.0f;

int max_iter = 360;
int pal = 0; // 0=hsv 1=rgb
int show_z;
FP_TYPE c_x = 0.15f;
FP_TYPE c_y = -0.60f;
// FP_TYPE c_x = -0.7f;
// FP_TYPE c_y = 0.27015f;

char status_line[200];

int cur_dev = 1; // 0 - CPU, 1,..nr_devices - OCL devices

enum fractals fractal = JULIA;

int gws_x = WIDTH / 4;
int gws_y = HEIGHT / 4;
unsigned long render_time;
unsigned long frame_time;
int draw_frames = 16;

int prepare_pixels(struct ocl_device* dev)
{
    int err;
    if (!dev->initialized) return 0;
    void* pixels;

    if (posix_memalign((void**)&pixels, 4096, IMAGE_SIZE)) return 1;
    dev->cl_pixels = clCreateBuffer(dev->ctx, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR, IMAGE_SIZE, pixels, &err);

    if (err != CL_SUCCESS)
    {
        printf("clCreateBuffer pixels returned %d\n", err);
        return 1;
    }
    printf("OCL buffer created with size=%d\n", IMAGE_SIZE);
    return 0;
}

int prepare_colors(struct ocl_device* dev)
{
    // h [0..359]
    // s [1]
    // v [0, 1]

    int err, v, iter;

    if (!dev->initialized) return 0;
    if (!colors)
    {
        err = posix_memalign((void**)&colors, 4096, 4096);
        if (err) return 1;

        for (v = 0; v < 2; v++)
        {
            for (iter = 0; iter < 360; iter++)
            {
                float h1 = (iter % 360) / 60.0;
                float v1 = 1.0 * v;
                int r, g, b;
                float r1, g1, b1, i, f, p, q, t;

                i = floor(h1);
                f = h1 - i;
                p = 0;
                q = v1 * (1.0 - f);
                t = v1 * (1.0 - (1.0 - f));
                switch ((int)i)
                {
                case 0:
                    r1 = v1;
                    g1 = t;
                    b1 = p;
                    break;
                case 1:
                    r1 = q;
                    g1 = v1;
                    b1 = p;
                    break;
                case 2:
                    r1 = p;
                    g1 = v1;
                    b1 = t;
                    break;
                case 3:
                    r1 = p;
                    g1 = q;
                    b1 = v1;
                    break;
                case 4:
                    r1 = t;
                    g1 = p;
                    b1 = v1;
                    break;
                case 5:
                    r1 = v1;
                    g1 = p;
                    b1 = q;
                    break;
                }
                r = roundf(255.0 * r1);
                r &= 0xff;
                g = roundf(255.0 * g1);
                g &= 0xff;
                b = roundf(255.0 * b1);
                b &= 0xff;
                colors[iter + v * 360] = 0xff000000 | r << 16 | g << 8 | b;
            }
        }
        colors[0] = 0;
        colors[360] = 0;
    }
    dev->cl_colors = clCreateBuffer(dev->ctx, CL_MEM_READ_ONLY | CL_MEM_USE_HOST_PTR, 4096, colors, &err);
    if (err != CL_SUCCESS)
    {
        printf("%s clCreateBuffer colors returned %d\n", dev->name, err);
        return 1;
    }

    return 0;
}

int finish_thread;

int set_kernel_arg(cl_kernel kernel, char* name, int i, int size, void* arg)
{
    int err;

    err = clSetKernelArg(kernel, i, size, arg);
    if (err != CL_SUCCESS)
    {
        printf("set_kernel_arg for: %s(i=%d size=%d) returned %d\n", name, i, size, err);
        return 1;
    }
    return 0;
}

int execute_fractal(struct ocl_device* dev, enum fractals fractal)
{
    size_t gws[2];
    size_t ofs[2] = {0, 0};
    cl_kernel kernel = dev->kernels[fractal];
    char* name = fractals[fractal].name;
    struct kernel_args* args = &dev->args[fractal];
    int err;
    unsigned long tp1, tp2;
    int fp_size = dev->fp64 ? sizeof(double) : sizeof(float);

    args->ofs_lx = ofs_lx;
    args->ofs_rx = ofs_rx;
    args->ofs_ty = ofs_ty;
    args->ofs_by = ofs_by;

    FP_TYPE ofs_lx1 = (args->ofs_lx + dx) / szx;
    FP_TYPE ofs_rx1 = (args->ofs_rx + dx) / szx;
    FP_TYPE ofs_ty1 = (args->ofs_ty + dy) / szy;
    FP_TYPE ofs_by1 = (args->ofs_by + dy) / szy;
    FP_TYPE step_x = (ofs_rx1 - ofs_lx1) / WIDTH_FL;
    FP_TYPE step_y = (ofs_by1 - ofs_ty1) / HEIGHT_FL;

    args->mm = mm;
    args->er = er;
    args->max_iter = max_iter;
    args->pal = pal;
    args->show_z = show_z;
    args->c_x = c_x;
    args->c_y = c_y;
    args->ofs_x++;
    if (args->ofs_x == 4)
    {
        args->ofs_y++;
        args->ofs_x = 0;
    }
    if (args->ofs_y == 4)
    {
        args->ofs_x = 0;
        args->ofs_y = 0;
    }

    gws[0] = gws_x;
    gws[1] = gws_y;

    if (set_kernel_arg(kernel, name, 0, sizeof(cl_mem), &dev->cl_pixels)) return 1;
    if (set_kernel_arg(kernel, name, 1, sizeof(cl_mem), &dev->cl_colors)) return 1;
    if (set_kernel_arg(kernel, name, 2, sizeof(cl_int), &mm)) return 1;
    if (dev->fp64)
    {
        if (set_kernel_arg(kernel, name, 3, fp_size, &ofs_lx1)) return 1;
        if (set_kernel_arg(kernel, name, 4, fp_size, &step_x)) return 1;
        if (set_kernel_arg(kernel, name, 5, fp_size, &ofs_ty1)) return 1;
        if (set_kernel_arg(kernel, name, 6, fp_size, &step_y)) return 1;
        if (set_kernel_arg(kernel, name, 7, fp_size, &args->er)) return 1;
    }
    else
    {
        float fofs_lx1 = (float)ofs_lx1;
        float fofs_ty1 = (float)ofs_ty1;

        float fstep_x = (float)step_x;
        float fstep_y = (float)step_y;

        float fer = (float)args->er;

        if (set_kernel_arg(kernel, name, 3, fp_size, &fofs_lx1)) return 1;
        if (set_kernel_arg(kernel, name, 4, fp_size, &fstep_x)) return 1;
        if (set_kernel_arg(kernel, name, 5, fp_size, &fofs_ty1)) return 1;
        if (set_kernel_arg(kernel, name, 6, fp_size, &fstep_y)) return 1;
        if (set_kernel_arg(kernel, name, 7, fp_size, &fer)) return 1;
    }
    if (set_kernel_arg(kernel, name, 8, sizeof(int), &args->max_iter)) return 1;
    if (set_kernel_arg(kernel, name, 9, sizeof(int), &args->pal)) return 1;
    if (set_kernel_arg(kernel, name, 10, sizeof(int), &args->show_z)) return 1;
    if (fractal == JULIA || fractal == JULIA_FULL || fractal == DRAGON || fractal == JULIA3)
    {
        if (dev->fp64)
        {
            if (set_kernel_arg(kernel, name, 11, fp_size, &args->c_x)) return 1;
            if (set_kernel_arg(kernel, name, 12, fp_size, &args->c_y)) return 1;
        }
        else
        {
            float fc_x = (float)args->c_x;
            float fc_y = (float)args->c_y;

            if (set_kernel_arg(kernel, name, 11, fp_size, &fc_x)) return 1;
            if (set_kernel_arg(kernel, name, 12, fp_size, &fc_y)) return 1;
        }

        if (fractal == JULIA || fractal == JULIA3)
        {
            if (set_kernel_arg(kernel, name, 13, sizeof(int), &args->ofs_x)) return 1;
            if (set_kernel_arg(kernel, name, 14, sizeof(int), &args->ofs_y)) return 1;
        }
    }
    if (fractal == MANDELBROT)
    {
        if (set_kernel_arg(kernel, name, 11, sizeof(int), &args->ofs_x)) return 1;
        if (set_kernel_arg(kernel, name, 12, sizeof(int), &args->ofs_y)) return 1;
    }

    // err = clEnqueueNDRangeKernel(dev->queue, kernel, 2, ofs, gws, NULL, 0,
    // NULL, &dev->event);
    //
    //    printf("%s: clEnqueueNDRangeKernel %s\n", dev->name, name);

    tp1 = get_time_usec();

    err = clEnqueueNDRangeKernel(dev->queue, kernel, 2, ofs, gws, NULL, 0, NULL, NULL);
    if (err != CL_SUCCESS)
    {
        printf("%s: clEnqueueNDRangeKernel %s returned %d\n", dev->name, name, err);
        return 1;
    }
    // clFlush(dev->queue);
    // clWaitForEvents(1, &dev->event);
    clFinish(dev->queue);
    tp2 = get_time_usec();
    dev->execution = tp2 - tp1;

    //  clReleaseEvent(dev->event);

    // clFinish(cl[thread].queue_gpu);
    return 0;
}

void* ocl_kernel(void* d)
{
    struct ocl_device* dev = (struct ocl_device*)d;
    struct ocl_thread* t = &dev->thread;
    int err = 0;

    while (!finish_thread && !err)
    {
        //        printf("%s: waiting for work\n", dev->name);
        pthread_mutex_lock(&t->lock);
        while (!t->work)
        {
            if (finish_thread)
            {
                printf("%s: thread exits\n", dev->name);
                return NULL;
            }
            pthread_cond_wait(&t->cond, &t->lock);
        }
        t->work = 0;
        pthread_mutex_unlock(&t->lock);

        //        printf("ocl kernel for %s tid=%lx\n", dev->name,
        //        dev->thread.tid);

        err = execute_fractal(dev, fractal);

        if (err)
        {
            printf("thread interrupted\n");
            dev->thread.finished = 1;
            nr_devices--;
        }

        pthread_mutex_lock(&lock_fin);
        tasks_finished++;
        pthread_cond_signal(&cond_fin);
        pthread_cond_broadcast(&cond_fin);
        pthread_mutex_unlock(&lock_fin);
    }
    printf("%s: thread exits\n", dev->name);
    sleep(1);
    return NULL;
}

int signal_device(struct ocl_device* dev)
{
    //	printf("signal current device: %s\n", dev->name);
    if (!dev->initialized) return 1;
    if (dev->thread.finished)
    {
        printf("%s: thread already finished\n", dev->name);
        return 1;
    }

    pthread_mutex_lock(&dev->thread.lock);
    dev->thread.work = 1;
    pthread_cond_signal(&dev->thread.cond);
    pthread_mutex_unlock(&dev->thread.lock);
    return 0;
}

struct cpu_args
{
    int xs, xe, ys, ye, ofs_x, ofs_y;
};

unsigned long cpu_execution;

void* execute_fractal_cpu(void* c)
{
    int x, y;
    struct cpu_args* cpu = (struct cpu_args*)c;

    FP_TYPE ofs_lx1 = (ofs_lx + dx) / szx;
    FP_TYPE ofs_rx1 = (ofs_rx + dx) / szx;
    FP_TYPE ofs_ty1 = (ofs_ty + dy) / szy;
    FP_TYPE ofs_by1 = (ofs_by + dy) / szy;

    FP_TYPE step_x = (ofs_rx1 - ofs_lx1) / WIDTH_FL;
    FP_TYPE step_y = (ofs_by1 - ofs_ty1) / HEIGHT_FL;

    for (y = cpu->ys; y < cpu->ye; y++)
    {
        for (x = cpu->xs; x < cpu->xe; x++)
        {
            switch (fractal)
            {
            case JULIA:
                julia(cpu->ofs_x + x * 4, cpu->ofs_y + y * 4, cpu_pixels, colors, mm, ofs_lx, step_x, ofs_ty, step_y, er, max_iter, pal, show_z, c_x,
                      c_y);
                break;
            case JULIA3:
                julia3(cpu->ofs_x + x * 4, cpu->ofs_y + y * 4, cpu_pixels, colors, mm, ofs_lx, step_x, ofs_ty, step_y, er, max_iter, pal, show_z, c_x,
                       c_y);
                break;
            case JULIA_FULL:
                julia_full(x, y, cpu_pixels, colors, mm, ofs_lx, step_x, ofs_ty, step_y, er, max_iter, pal, show_z, c_x, c_y);
                break;
            case MANDELBROT:
                mandelbrot(cpu->ofs_x + x * 4, cpu->ofs_y + y * 4, cpu_pixels, colors, mm, ofs_lx, step_x, ofs_ty, step_y, er, max_iter, pal, show_z);
                break;
            default:
                return NULL;
            }
        }
    }
    return NULL;
}

void start_cpu()
{
    unsigned long tp1, tp2;
    static int ofs_x = 0, ofs_y = 0;
    int t;

    ofs_x++;
    if (ofs_x == 4)
    {
        ofs_y++;
        ofs_x = 0;
    }
    if (ofs_y == 4)
    {
        ofs_x = 0;
        ofs_y = 0;
    }

    tp1 = get_time_usec();

    if (fractal == DRAGON)
    {
        memset(cpu_pixels, 0, IMAGE_SIZE);
        dragon(0, 0, cpu_pixels, colors, mm, ofs_lx, 0, ofs_ty, 0, er, max_iter, pal, show_z, c_x, c_y);
    }
    else
    {
        struct cpu_args t_args[16] = {{0, gws_x / 4, 0, gws_y / 4, ofs_x, ofs_y},
                                      {gws_x / 4, gws_x / 2, 0, gws_y / 4, ofs_x, ofs_y},
                                      {gws_x / 2, gws_x * 3 / 4, 0, gws_y / 4, ofs_x, ofs_y},
                                      {gws_x * 3 / 4, gws_x, 0, gws_y / 4, ofs_x, ofs_y},

                                      {0, gws_x / 4, gws_y / 4, gws_y / 2, ofs_x, ofs_y},
                                      {gws_x / 4, gws_x / 2, gws_y / 4, gws_y / 2, ofs_x, ofs_y},
                                      {gws_x / 2, gws_x * 3 / 4, gws_y / 4, gws_y / 2, ofs_x, ofs_y},
                                      {gws_x * 3 / 4, gws_x, gws_y / 4, gws_y / 2, ofs_x, ofs_y},

                                      {0, gws_x / 4, gws_y / 2, gws_y * 3 / 4, ofs_x, ofs_y},
                                      {gws_x / 4, gws_x / 2, gws_y / 2, gws_y * 3 / 4, ofs_x, ofs_y},
                                      {gws_x / 2, gws_x * 3 / 4, gws_y / 2, gws_y * 3 / 4, ofs_x, ofs_y},
                                      {gws_x * 3 / 4, gws_x, gws_y / 2, gws_y * 3 / 4, ofs_x, ofs_y},

                                      {0, gws_x / 4, gws_y * 3 / 4, gws_y, ofs_x, ofs_y},
                                      {gws_x / 4, gws_x / 2, gws_y * 3 / 4, gws_y, ofs_x, ofs_y},
                                      {gws_x / 2, gws_x * 3 / 4, gws_y * 3 / 4, gws_y, ofs_x, ofs_y},
                                      {gws_x * 3 / 4, gws_x, gws_y * 3 / 4, gws_y, ofs_x, ofs_y}};

        pthread_t tid[16];
        for (t = 0; t < 16; t++)
        {
            pthread_create(&tid[t], NULL, execute_fractal_cpu, &t_args[t]);
        }
        for (t = 0; t < 16; t++)
        {
            pthread_join(tid[t], NULL);
        }
    }
    tp2 = get_time_usec();
    cpu_execution = tp2 - tp1;
    SDL_UpdateTexture(texture, NULL, cpu_pixels, WIDTH * 4);
}

void start_ocl()
{
    int err;
    if (!nr_devices) return;

    if (signal_device(&ocl_devices[current_device]))
    {
        printf("can't signal device\n");
        return;
    }

    pthread_mutex_lock(&lock_fin);
    //    printf("waiting for finished tasks\n");
    while (!tasks_finished)
    {
        pthread_cond_wait(&cond_fin, &lock_fin);
    }
    //    printf("tasks finished\n");
    tasks_finished = 0;
    pthread_mutex_unlock(&lock_fin);

    if (ocl_devices[current_device].initialized)
    {
        void* px1;

        px1 = clEnqueueMapBuffer(ocl_devices[current_device].queue, ocl_devices[current_device].cl_pixels, CL_TRUE, CL_MAP_READ | CL_MAP_WRITE, 0,
                                 IMAGE_SIZE, 0, NULL, NULL, &err);
        if (err != CL_SUCCESS)
        {
            printf("clEnqueueMapBuffer error %d\n", err);
        }
        else
        {
            SDL_UpdateTexture(texture, NULL, px1, WIDTH * 4);
            if (fractal == DRAGON) memset(px1, 0, IMAGE_SIZE);
            clEnqueueUnmapMemObject(ocl_devices[current_device].queue, ocl_devices[current_device].cl_pixels, px1, 0, NULL, NULL);
        }
    }
}

int prepare_thread(struct ocl_device* dev)
{
    if (!dev->initialized) return 0;

    dev->thread.work = 0;
    if (pthread_mutex_init(&dev->thread.lock, NULL)) return 1;
    if (pthread_cond_init(&dev->thread.cond, NULL)) return 1;
    pthread_create(&dev->thread.tid, NULL, ocl_kernel, dev);
    return 0;
}

unsigned long draw_one_frame()
{
    unsigned long tp1, tp2;

    tp1 = get_time_usec();

    if (cur_dev)
    {
        start_ocl();
    }
    else
    {
        start_cpu();
    }
    tp2 = get_time_usec();
    return tp2 - tp1;
}
void draw_right_panel()
{
    int raw = 0;
    draw_string(raw++, "===", " Main ====");
    draw_int(raw++, "F1-F5 fractal", fractal);
    draw_int(raw++, "v device", cur_dev);
    draw_int(raw++, "1 show_z", show_z);
    draw_double(raw++, "ofs_lx", ofs_lx);
    draw_double(raw++, "ofs_rx", ofs_rx);
    draw_double(raw++, "ofs_ty", ofs_ty);
    draw_double(raw++, "ofs_by", ofs_by);

    raw++;
    draw_string(raw++, "==", " Parameters ===");
    draw_int(raw++, "u/i iter", max_iter);
    draw_double(raw++, "z/x er", er);
    draw_double(raw++, "[/] c_x", c_x);
    draw_double(raw++, "-/= c_y", c_y);
    draw_int(raw++, "2/3 gws_x", gws_x);
    draw_int(raw++, "2/3 gws_y", gws_y);
    draw_double(raw++, "zoom", zoom);
    draw_double(raw++, "LB/RB zx", zx);
    draw_double(raw++, "LB/RB zy", zy);

    raw++;
    draw_string(raw++, "===", " Colors ====");
    draw_string(raw++, "h pal", (pal) ? "RGB" : "HSV");
    draw_hex(raw++, "o/p mm", mm);
    draw_hex(raw++, "k/l mm", mm);
    draw_hex(raw++, "n/m mm", mm);

    raw++;
    draw_string(raw++, "===", " Moves ====");
    draw_double(raw++, "Left/Right szx", szx);
    draw_double(raw++, "Down/Up szy", szy);
    draw_double(raw++, ",/. szx", szx);
    draw_double(raw++, ",/. szy", szy);
    draw_double(raw++, "a/d dx", dx);
    draw_double(raw++, "w/s dy", dy);

    raw++;
    draw_string(raw++, "=", " Benchmarking ==");
    draw_long(raw++, "time", frame_time / draw_frames);
    draw_long(raw++, "exec", cur_dev ? ocl_devices[current_device].execution : cpu_execution);
    draw_long(raw++, "render", render_time);
}

void run_program()
{
    SDL_Event event;
    int button;
    int animate = 0;
    int key = 0;
    int stop_animation = 1;
    int flip_window = 0;
    double m1x, m1y;
    int i;
    int draw = 1;
    SDL_Rect window_rec;

    if (init_ocl()) return;
    init_window();

    window_rec.w = WIDTH;
    window_rec.h = HEIGHT;
    window_rec.x = 0;
    window_rec.y = 0;

    if (pthread_mutex_init(&lock_fin, NULL)) return;
    if (pthread_cond_init(&cond_fin, NULL)) return;

    for (i = 0; i < nr_devices; i++)
    {
        if (prepare_colors(&ocl_devices[i])) return;
        if (prepare_pixels(&ocl_devices[i])) return;
        if (prepare_thread(&ocl_devices[i])) return;
    }

    if (posix_memalign((void**)&cpu_pixels, 4096, IMAGE_SIZE)) return;
    while (1)
    {
        if (animate || key)
        {
            ofs_lx = (ofs_lx - mx) * zx + mx;
            ofs_rx = (ofs_rx - mx) * zx + mx;

            ofs_ty = (ofs_ty - my) * zy + my;
            ofs_by = (ofs_by - my) * zy + my;

            zoom = (OFS_RX - OFS_LX) / (ofs_rx - ofs_lx);

            if (ofs_lx < -10 || ofs_rx > 10 || ofs_ty > 10 || ofs_by < -10 ||
                ((OFS_RX - OFS_LX) / (ofs_rx - ofs_lx) > (ocl_devices[current_device].fp64 ? 43000000000000 : 300000)))
            {
                dx = 0;
                dy = 0;

                draw = 0;
                animate = 0;
                draw_frames = 16;
                stop_animation = 1;
            }
        }
        if (fractal == DRAGON) draw_frames = 1;

        if (draw || stop_animation)
        {
            stop_animation = 0;
            flip_window = 1;
        }
        else
        {
            SDL_Delay(1);
        }

        if (flip_window)
        {
            float m2x, m2y;
            int pixel;
            unsigned long tp1, tp2;

            flip_window = 0;
            frame_time = 0;

            tp1 = get_time_usec();

            for (pixel = 0; pixel < draw_frames; pixel++)
            {
                frame_time += draw_one_frame();
            }
            SDL_RenderCopy(main_window, texture, NULL, &window_rec);

            m2x = equation(m1x, 0.0f, ofs_lx, WIDTH_FL, ofs_rx);
            m2y = equation(m1y, 0.0f, ofs_ty, HEIGHT_FL, ofs_by);

            draw_right_panel();

            sprintf(status_line, "[%2.20f,%2.20f] %s", m2x, m2y, cur_dev ? "OCL" : "CPU");
            write_text(status_line, 0, HEIGHT - FONT_SIZE);
            if (cur_dev)
            {
                sprintf(status_line, "OCL[%d/%d]: %s %s gws=[%3d,%3d] ", current_device + 1, nr_devices, ocl_devices[current_device].name,
                        ocl_devices[current_device].fp64 ? "fp64" : "fp32", gws_x, gws_y);
                write_text(status_line, 0, HEIGHT - 2 * FONT_SIZE);
            }
            SDL_RenderPresent(main_window);
            tp2 = get_time_usec();
            render_time = tp2 - tp1;
            //		    printf("render time=%lu\n", render_time);
        }
        if (!animate) draw = 0;
        if (key)
        {
            draw = 0;
            key = 0;
        }

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT) goto finish;
            if (event.type == SDL_KEYDOWN)
            {
                int kl = event.key.keysym.sym;
                switch (kl)
                {
                case 27:
                    goto finish;
                case 'u':
                    max_iter -= 10;
                    if (max_iter < 10) max_iter = 10;
                    break;
                case 'i':
                    max_iter += 10;
                    break;
                case 'o':
                    mm++;
                    break;
                case 'p':
                    mm--;
                    if (!mm) mm = 1;
                    break;
                case 'k':
                    mm += 100;
                    break;
                case 'l':
                    mm -= 100;
                    if (mm < 1) mm = 1;
                    break;
                case 'n':
                    mm += 1000;
                    break;
                case 'm':
                    mm -= 1000;
                    if (mm < 1) mm = 1;
                    break;

                case SDLK_LEFT:
                    szx -= 0.01 / zoom;
                    if (szx < 0.1) szx = 0.1;
                    key = 1;
                    break;
                case SDLK_RIGHT:
                    szx += 0.01 / zoom;
                    key = 1;
                    break;
                case SDLK_DOWN:
                    szy -= 0.01 / zoom;
                    if (szy < 0.1) szy = 0.1;
                    key = 1;
                    break;
                case SDLK_UP:
                    szy += 0.01 / zoom;
                    key = 1;
                    break;
                case 'a':
                    dx -= 0.1 / zoom;
                    key = 1;
                    break;
                case 'd':
                    dx += 0.1 / zoom;
                    key = 1;
                    break;
                case 's':
                    dy -= 0.1 / zoom;
                    key = 1;
                    break;
                case 'w':
                    dy += 0.1 / zoom;
                    key = 1;
                    break;
                case 'z':
                    er -= 0.1;
                    if (er < 0.0f) er = 0.0f;
                    break;
                case 'x':
                    er += 0.1;
                    break;
                case SDLK_COMMA:
                    szx += 1.0;
                    szy += 1.0;
                    key = 1;
                    break;
                case SDLK_PERIOD:
                    szx -= 1.0;
                    szy -= 1.0;
                    if (szy < 0.1) szy = 0.1;
                    if (szx < 0.1) szx = 0.1;
                    key = 1;
                    break;
                case 'h':
                    pal ^= 1;
                    break;
                case '[':
                    c_x -= 0.001;
                    break;
                case ']':
                    c_x += 0.001;
                    break;
                case '1':
                    show_z ^= 1;
                    break;
                case '2':
                    gws_x *= 2;
                    if (gws_x > WIDTH) gws_x = WIDTH;
                    gws_y *= 2;
                    if (gws_y > HEIGHT) gws_y = HEIGHT;
                    printf("gws: x=%d y=%d\n", gws_x, gws_y);
                    break;
                case '3':
                    gws_x /= 2;
                    if (gws_x < 8) gws_x = 8;
                    gws_y /= 2;
                    if (gws_y < 8) gws_y = 8;
                    printf("gws: x=%d y=%d\n", gws_x, gws_y);
                    break;
                case '-':
                    c_y -= 0.001;
                    break;
                case '=':
                    c_y += 0.001;
                    break;
                case SDLK_F1:
                    fractal = JULIA;
                    gws_x = WIDTH / 4;
                    gws_y = HEIGHT / 4;
                    max_iter = 360;
                    er = 4.0f;
                    ofs_lx = -1.5f;
                    ofs_ty = 1.5f;
                    break;
                case SDLK_F2:
                    fractal = MANDELBROT;
                    gws_x = WIDTH / 4;
                    gws_y = HEIGHT / 4;
                    max_iter = 360;
                    er = 4.0f;
                    ofs_lx = -1.5f;
                    ofs_ty = 1.5f;
                    break;
                case SDLK_F3:
                    fractal = JULIA_FULL;
                    gws_x = WIDTH;
                    gws_y = HEIGHT;
                    max_iter = 360;
                    er = 4.0f;
                    ofs_lx = -1.5f;
                    ofs_ty = 1.5f;
                    break;
                case SDLK_F4:
                    fractal = DRAGON;
                    gws_x = WIDTH;
                    gws_y = HEIGHT;
                    max_iter = 10000;
                    er = 0.9f;
                    ofs_lx = 0.0f;
                    ofs_ty = 0.0f;
                    if (ocl_devices[current_device].initialized)
                    {
                        void* px1;
                        int err;

                        px1 = clEnqueueMapBuffer(ocl_devices[current_device].queue, ocl_devices[current_device].cl_pixels, CL_TRUE,
                                                 CL_MAP_READ | CL_MAP_WRITE, 0, IMAGE_SIZE, 0, NULL, NULL, &err);
                        if (err != CL_SUCCESS)
                        {
                            printf("clEnqueueMapBuffer "
                                   "error %d\n",
                                   err);
                        }
                        else
                        {
                            memset(px1, 0, IMAGE_SIZE);
                            clEnqueueUnmapMemObject(ocl_devices[current_device].queue, ocl_devices[current_device].cl_pixels, px1, 0, NULL, NULL);
                        }
                    }
                    break;
                case SDLK_F5:
                    fractal = JULIA3;
                    gws_x = WIDTH / 4;
                    gws_y = HEIGHT / 4;
                    max_iter = 360;
                    er = 4.0f;
                    ofs_lx = -1.5f;
                    ofs_ty = 1.5f;
                    break;
                case 'v':
                    cur_dev++;
                    if (cur_dev > nr_devices) cur_dev = 0;
                    if (cur_dev)
                    {
                        current_device = cur_dev - 1;
                    }
                    break;
                }
                draw = 1;
                draw_frames = 16;
            }

            if (event.type == SDL_MOUSEMOTION)
            {
                if (event.button.x > WIDTH) continue;
                m1x = event.button.x;
                m1y = event.button.y;
                flip_window = 1;
                draw_frames = 1;
            }

            if (event.type == SDL_MOUSEBUTTONDOWN)
            {
                if (event.button.x > WIDTH) continue;
                if (event.button.button == 2)
                {
                    dx = 0;
                    dy = 0;

                    draw = 0;
                    animate = 0;
                    draw_frames = 16;
                    stop_animation = 1;
                    continue;
                }

                mx = equation(event.button.x, 0, ofs_lx, WIDTH, ofs_rx);
                my = equation(event.button.y % (HEIGHT), 0, ofs_ty, HEIGHT, ofs_by);

                if (event.button.button == 3)
                {
                    if (button != event.button.button)
                    {
                        zx = 1.0;
                        zy = 1.0;
                        stop_animation = 1;
                        draw_frames = 16;
                    }
                    else
                    {
                        zx += 0.001;
                        zy += 0.001;
                    }
                    animate = 1;
                    draw_frames = 1;
                }

                if (event.button.button == 1)
                {
                    if (button != event.button.button)
                    {
                        zx = 1.0;
                        zy = 1.0;
                        stop_animation = 1;
                        draw_frames = 16;
                    }
                    else
                    {
                        zx -= 0.001;
                        zy -= 0.001;
                    }
                    animate = 1;
                    draw_frames = 1;
                }
                button = event.button.button;

                draw = 1;
            }
            if (event.type == SDL_MOUSEBUTTONUP)
            {
                //   animate = 0;
            }
        }
    }
finish:
    finish_thread = 1;
    pthread_mutex_destroy(&lock_fin);
    pthread_cond_destroy(&cond_fin);

    close_ocl();
    SDL_Quit();
}

int main()
{
    srandom(time(0));

    run_program();
    return 0;
}
