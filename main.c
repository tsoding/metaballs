#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#endif // _WIN32

#define LA_IMPLEMENTATION
#include "la.h"

#define PROF
#include "prof.c"

typedef uint32_t Pixel32;

static float Q_rsqrt( float number )
{
    static_assert(sizeof( float ) == sizeof( uint32_t ),
                  "FAST_RSQRT does not work on this architecture");

    uint32_t i;
    float x2, y;
    const float threehalfs = 1.5F;

    x2 = number * 0.5F;
    y  = number;
    i  = * ( uint32_t * ) &y;                   // evil floating point bit level hacking
    i  = 0x5f3759df - ( i >> 1 );               // what the fuck?
    y  = * ( float * ) &i;
    y  = y * ( threehalfs - ( x2 * y * y ) );   // 1st iteration
//	y  = y * ( threehalfs - ( x2 * y * y ) );   // 2nd iteration, this can be removed

    return y;
}

static Pixel32 blend_pixels(Pixel32 a, Pixel32 b, float p)
{
    // 0xRRGGBB
    float ar = (float) ((a >> (8 * 2)) & 0xFF);
    float br = (float) ((b >> (8 * 2)) & 0xFF);
    Pixel32 nr = (Pixel32) lerpf(ar, br, p);

    float ag = (float) ((a >> (8 * 1)) & 0xFF);
    float bg = (float) ((b >> (8 * 1)) & 0xFF);
    Pixel32 ng = (Pixel32) lerpf(ag, bg, p);

    float ab = (float) ((a >> (8 * 0)) & 0xFF);
    float bb = (float) ((b >> (8 * 0)) & 0xFF);
    Pixel32 nb = (Pixel32) lerpf(ab, bb, p);

    return (nr << (8 * 2)) | (ng << (8 * 1)) | (nb << (8 * 0));
}

#define FAST_RSQRT

#define SQRT_FALLOFF

static void render_scene(Pixel32 *pixels, size_t width, size_t height,
                         Pixel32 background,
                         V2f ball1, Pixel32 ball1_color,
                         V2f ball2, Pixel32 ball2_color)
{
    for (int y = 0; (size_t) y < height; ++y) {
        for (int x = 0; (size_t) x < width; ++x) {
            V2f p = v2f_sum(v2f(x, y), v2ff(0.5));
#ifdef SQRT_FALLOFF
#  ifdef FAST_RSQRT
            float s1 = Q_rsqrt(v2f_sqrlen(v2f_sub(ball1, p)));
            float s2 = Q_rsqrt(v2f_sqrlen(v2f_sub(ball2, p)));
#  else
            float s1 = 1.0f/sqrtf(v2f_sqrlen(v2f_sub(ball1, p)));
            float s2 = 1.0f/sqrtf(v2f_sqrlen(v2f_sub(ball2, p)));
#  endif // FAST_RSQRT
#else
            float s1 = 1.0f / v2f_sqrlen(v2f_sub(ball1, p));
            s1 *= s1;
            float s2 = 1.0f / v2f_sqrlen(v2f_sub(ball2, p));
            s2 *= s2;
#endif // SQRT_FALLOFF
            float s = s1 + s2;

            if (s >= 0.005f) {
                pixels[y*width + x] = blend_pixels(ball1_color, ball2_color, s1 / s);
            } else {
                pixels[y*width + x] = background;
            }
        }
    }
}

#define WIDTH (16 * 100)
#define HEIGHT (9 * 100)
#define BACKGROUND 0x5555AA

#ifdef _WIN32
HBITMAP hbmp;
HANDLE hTickThread;
HWND hwnd;
HDC hdcMem;
#endif

static Pixel32 *pixels;

#define MIT_SHM_RENDER

#ifndef _WIN32

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "ERROR: could not open the default display\n");
        exit(1);
    }

    Bool mit_shm_available = XShmQueryExtension(display);
    if (!mit_shm_available) {
        fprintf(stderr, "WARNING: could not find MIT-SHM extension\n");
    } else {
        fprintf(stderr, "INFO: detected MIT-SHM extension\n");
    }

    Window window = XCreateSimpleWindow(
                        display,
                        XDefaultRootWindow(display),
                        0, 0,
                        WIDTH, HEIGHT,
                        0,
                        0,
                        0);

    XWindowAttributes wa = {0};
    XGetWindowAttributes(display, window, &wa);

    XImage *image;
    XShmSegmentInfo shminfo = {0};
    if (mit_shm_available) {
        shminfo.readOnly = True;
        shminfo.shmid = shmget(IPC_PRIVATE, WIDTH*HEIGHT*sizeof(Pixel32), IPC_CREAT|0777);
        if (shminfo.shmid < 0) {
            fprintf(stderr, "ERROR: Could not create a new shared memory segment: %s\n",
                    strerror(errno));
            exit(1);
        }

        pixels = shmat(shminfo.shmid, 0, 0);
        shminfo.shmaddr = (char*) pixels;
        if (shminfo.shmaddr == (void*) -1) {
            fprintf(stderr, "ERROR: could not memory map the shared memory segment: %s\n",
                    strerror(errno));
            exit(1);
        }

        if (!XShmAttach(display, &shminfo)) {
            fprintf(stderr, "ERROR: could not attach the shared memory segment to the server\n");
            exit(1);
        }

        image = XShmCreateImage(display,
                                wa.visual,
                                wa.depth,
                                ZPixmap,
                                (char *) pixels,
                                &shminfo,
                                WIDTH,
                                HEIGHT);
    } else {
        pixels = mmap(NULL,
                      WIDTH*HEIGHT*sizeof(Pixel32),
                      PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS,
                      -1,
                      0);
        if (pixels == MAP_FAILED) {
            fprintf(stderr, "ERROR: Could not allocate memory for pixels: %s\n",
                    strerror(errno));
            exit(1);
        }
        image = XCreateImage(display,
                             wa.visual,
                             wa.depth,
                             ZPixmap,
                             0,
                             (char*) pixels,
                             WIDTH,
                             HEIGHT,
                             32,
                             WIDTH * sizeof(Pixel32));
    }

    GC gc = XCreateGC(display, window, 0, NULL);

    Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete_window, 1);

    XSelectInput(display, window, KeyPressMask | PointerMotionMask);

    XMapWindow(display, window);

    float global_time = 0.0f;

    V2f ball2 = v2ff(0.0f);

    int CompletionType = XShmGetEventBase (display) + ShmCompletion;
    int SafeToRender = 1;

    int quit = 0;
    while (!quit) {
        while (XPending(display) > 0) {
            XEvent event = {0};
            XNextEvent(display, &event);
            switch (event.type) {
            case KeyPress: {
                switch (XLookupKeysym(&event.xkey, 0)) {
                case 'q':
                    quit = 1;
                    break;
                case 'p':
                    dump_summary(stdout);
                    break;
                }
            }
            break;

            case MotionNotify: {
                ball2 = v2f(event.xmotion.x, event.xmotion.y);
            }
            break;

            case ClientMessage: {
                if ((Atom) event.xclient.data.l[0] == wm_delete_window) {
                    quit = 1;
                }
            }
            break;

            default: {
                if (event.type == CompletionType) {
                    SafeToRender = 1;
                }
            }
            }
        }

        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
            fprintf(stderr, "ERROR: could not get current monotonic time: %s\n",
                    strerror(errno));
            exit(1);
        }
        global_time = (float) now.tv_sec + now.tv_nsec * 0.000000001f;

        V2f ball1 = v2ff(400.0f);
        // ball2 = v2f_sum(v2f_mul(v2f(WIDTH, HEIGHT), v2ff(0.5)),
        //                 v2f_mul(v2f(cosf(4.0f*global_time), sinf(4.0f*global_time)),
        //                         v2ff(HEIGHT * 0.25f)));

        if (SafeToRender) {
            clear_summary();
            begin_clock("TOTAL");
            {
                begin_clock("SCENE");
                render_scene(pixels, WIDTH, HEIGHT, BACKGROUND,
                             ball1, 0xEEEE22,
                             ball2, 0xEE22EE);
                end_clock();

                begin_clock("PutImage");
                if (mit_shm_available) {
                    XShmPutImage(display, window, gc, image, 0, 0, 0, 0, WIDTH, HEIGHT, True);
                    SafeToRender = 0;
                } else {
                    XPutImage(display, window, gc, image, 0, 0, 0, 0, WIDTH, HEIGHT);
                }
                end_clock();
            }
            end_clock();
        }
    }

    XCloseDisplay(display);

    return 0;
}

#else

// https://www.daniweb.com/programming/software-development/code/241875/fast-animation-with-the-windows-gdi

DWORD WINAPI tickThreadProc(HANDLE handle)
{
    Sleep(50);
    ShowWindow(hwnd, SW_SHOW);
    ShowCursor(FALSE);

    HDC hdc = GetDC(hwnd);

    hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmp);

    V2f ball1 = v2ff(400.0f);
    V2f ball2 = v2ff(0.0f);

    for (;;) {
        POINT p;
        if (GetCursorPos(&p) && ScreenToClient(hwnd, &p)) {
            ball2 = v2f(p.x, p.y);

            render_scene(pixels, WIDTH, HEIGHT, BACKGROUND, ball1, 0xEEEE22, ball2, 0xEE22EE);
            BitBlt(hdc, 0, 0, WIDTH, HEIGHT, hdcMem, 0, 0, SRCCOPY);
        }
    }

    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdc);
}

void MakeSurface(HWND hwnd)
{
    BITMAPINFO bmi;
    bmi.bmiHeader.biSize = sizeof(BITMAPINFO);
    bmi.bmiHeader.biWidth = WIDTH;
    bmi.bmiHeader.biHeight = -HEIGHT;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = 0;
    bmi.bmiHeader.biXPelsPerMeter = 0;
    bmi.bmiHeader.biYPelsPerMeter = 0;
    bmi.bmiHeader.biClrUsed = 0;
    bmi.bmiHeader.biClrImportant = 0;
    bmi.bmiColors[0].rgbBlue = 0;
    bmi.bmiColors[0].rgbGreen = 0;
    bmi.bmiColors[0].rgbRed = 0;
    bmi.bmiColors[0].rgbReserved = 0;

    HDC hdc = GetDC(hwnd);

    hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    DeleteDC(hdc);

    hTickThread = CreateThread(NULL, 0, &tickThreadProc, NULL, 0, NULL);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        MakeSurface(hwnd);
    }
    break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        BitBlt(hdc, 0, 0, WIDTH, HEIGHT, hdcMem, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
    }
    break;
    case WM_CLOSE: {
        DestroyWindow(hwnd);
    }
    break;
    case WM_DESTROY: {
        TerminateThread(hTickThread, 0);
        PostQuitMessage(0);
    }
    break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    WNDCLASSEX wc;
    MSG msg;

    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.hbrBackground = CreateSolidBrush(0);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    wc.hInstance = hInstance;
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = "animation_class";
    wc.lpszMenuName = NULL;
    wc.style = 0;

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, "Failed to register window class.", "Error", MB_OK);
        return 0;
    }

    hwnd = CreateWindowEx(
               WS_EX_APPWINDOW,
               "animation_class",
               "metaballs",
               WS_MINIMIZEBOX | WS_SYSMENU | WS_POPUP | WS_CAPTION,
               CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT,
               NULL, NULL, hInstance, NULL);

    while (GetMessage(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

#endif // _WIN32
