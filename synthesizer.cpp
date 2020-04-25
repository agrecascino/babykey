#include <iostream>
#include <math.h>
#include <portaudio.h>
#include <thread>
#include <mutex>
#include <functional>
#include <X11/Xlib.h>
#include <cstring>
#include <fstream>
using namespace std;
//import player;

struct NoteVoice {
    float volume = 0.0f;
    float pitch;
    float time = 0.0f;
    bool pressed = false;
    void reset(){
        pressed = true;
        time = 0.0f;
        volume = 0.125f;
    }
    void release() {
        pressed = false;
        //volume = 0.0f;
    }
    float simulate(float samplerate) {
        if(round(volume*16777216) < 1.0f)
            return 0.0f;
        if(!pressed)
            volume = (volume - (0.1f/samplerate));
        if(volume < 0.001f){
            volume = 0.0f;
        }
        float pitchc = pitch + 0.025*pitch*sin(time);
        time += 1.0f/samplerate;
        float sample = (
                    (sinf(2 * M_PI * pitchc * time)) +
                    (sinf(M_PI * pitchc * time)/6.0f) +
                    (sinf(2 * M_PI * pitchc * time * (3/2.0f))/12.0f) +
                    (sinf(2 * M_PI * pitchc * time * (5/2.0f))/7.0f)
                    ) * volume * min(time*10, 1.0f);
        return sample;
    }
};

struct Syntheizer {
    struct NoteVoice keys[88];
    Syntheizer(){
        for(int i = 0; i < 88; i++){
            keys[i].pitch = 27.5f * pow(2.0f, i/12.0f);
        }
    }
    float cframe(float samplerate) {
        float res = 0.0f;
        for(int i = 0; i < 88; i++){
            res += keys[i].simulate(samplerate);
        }
        return res;
    }
};

static int paCallback( const void *inputBuffer, void *outputBuffer,
                       unsigned long framesPerBuffer,
                       const PaStreamCallbackTimeInfo* timeInfo,
                       PaStreamCallbackFlags statusFlags,
                       void *userData );

enum KeyType {
    WhiteKey,
    BlackKey
};
struct Keyboard {
    Keyboard() {
        memset(key2note, 255, 256);
        Pa_OpenDefaultStream(&stream, 0, 1, paFloat32, 44100, 2048, paCallback, this);
        Pa_StartStream(stream);
        display = XOpenDisplay(NULL);
        screen = DefaultScreen(display);
        screen_cmap = DefaultColormap(display, DefaultScreen(display));
        XAutoRepeatOff(display);
        //If something breaks, it's gonna be this hacky sh*t.
        XLookupColor(display, screen_cmap, "Wheat", &Wheat, &Wheat);
        XAllocColor(display, screen_cmap, &Wheat);
        XLookupColor(display, screen_cmap, "Cornflower Blue", &Cornflower_Blue, &Cornflower_Blue);
        XAllocColor(display, screen_cmap, &Cornflower_Blue);
        window = XCreateSimpleWindow(display, RootWindow(display, screen), 10, 10, 840, 300, 1,
                                     BlackPixel(display, screen), Wheat.pixel);
        XSelectInput(display, window, KeyPressMask | KeyReleaseMask | ExposureMask);
        XMapWindow(display, window);
        XGCValues values;
        gc = XCreateGC(display, window, 0, &values);
        XSetFillStyle(display, gc, FillSolid);
        XSetForeground(display, gc, BlackPixel(display, screen));
        XSetBackground(display, gc, WhitePixel(display, screen));
        XSetLineAttributes(display, gc,
                           2, LineSolid, CapButt, JoinBevel);
        XSync(display, False);
        key2note[52] = 15 + 12;
        key2note[53] = 17 + 12;
        key2note[54] = 19 + 12;
        key2note[55] = 20 + 12;
        key2note[56] = 22 + 12;
        key2note[57] = 24 + 12;
        key2note[58] = 26 + 12;
        key2note[24] = 27 + 12;
        key2note[25] = 29 + 12;
        key2note[26] = 31 + 12;
        key2note[27] = 32 + 12;
        key2note[28] = 34 + 12;
        key2note[29] = 36 + 12;
        key2note[30] = 38 + 12;
        key2note[39] = 16 + 12;
        key2note[40] = 18 + 12;
        key2note[42] = 21 + 12;
        key2note[43] = 23 + 12;
        key2note[44] = 25 + 12;
        key2note[11] = 28 + 12;
        key2note[12] = 30 + 12;
        key2note[14] = 33 + 12;
        key2note[15] = 35 + 12;
        key2note[16] = 37 + 12;
    }
    int keyattach[10] = {1, 2, 4, 5, 6, 8, 9, 11, 12, 13};
    KeyType keys[24] = {WhiteKey, BlackKey, WhiteKey, BlackKey, WhiteKey, WhiteKey,
                        BlackKey, WhiteKey, BlackKey, WhiteKey, BlackKey, WhiteKey,
                        WhiteKey, BlackKey, WhiteKey, BlackKey, WhiteKey, WhiteKey,
                        BlackKey, WhiteKey, BlackKey, WhiteKey, BlackKey, WhiteKey};

    void draw_keyboard() {
        int nw = 0;
        int nb = 0;
        for(int i = 0; i < 24; i++) {
            if(keys[i] == WhiteKey) {

                if(!s.keys[27+i].pressed)
                    XSetForeground(display, gc, WhitePixel(display, screen));
                else
                    XSetForeground(display, gc, BlackPixel(display, screen));
                XFillRectangle(display, window, gc, nw*60, 200 - 150*(i/12), 60, 100);
                XFlush(display);
                nw++;
            }
            if(keys[i] == BlackKey) {
                if(!s.keys[27+i].pressed)
                    XSetForeground(display, gc, BlackPixel(display, screen));
                else
                    XSetForeground(display, gc, Cornflower_Blue.pixel);
                XFillRectangle(display, window, gc, (keyattach[nb] - 1)*60 + 30, 150 - 150*(i/12), 30, 100);
                XFlush(display);
                nb++;
            }
        }
    }

    void synthesize(float *obuffer) {
        //std::mutex mtx;
        //mtx.lock();
        for(int i = 0;i < 2048; i++)
            obuffer[i] = s.cframe(44100);
        //mtx.unlock();
    }
    uint16_t period2note[36] = { 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
                                 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
                                 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
                               };
    uint8_t key2note[256];

    /*void process_module_notes() {
        std::fstream f("love.mod");
        ModulePlayer mod(f);
        TrackerState s = mod.getState();
        Module internalmod = mod.getModule();
        int lastnotes[4] = {126, 126, 126, 126};
        uint64_t row = 0;
        uint64_t tick = 0;
        std::cout << "i hate you" <<  internalmod.norders << std::endl;
        for(uint64_t r = 0; r < internalmod.norders;) {
            while(true) {
                TickReturn ret = mod.PlayOneTick(r, row, tick);
                std::cout << "flow" << std::endl;
                for(int c = 0; c < 4; c++){
                    std::cout << "uh" << std::endl;
                    if(s.cstate[c].liveperiod && (s.cstate[c].livevolume > 0)) {
                        for(int i = 0; i < 36; i++) {
                            if(abs(period2note[i] - s.cstate[c].liveperiod) < 2) {
                                if(lastnotes[c] != (i + 3 +24)) {
                                    lastnotes[c] = i + 3 + 24;
                                    this->s.keys[i + 3 + 24].reset();
                                }
                            }
                         }
                    }
                    if((lastnotes[c] < 89) && (s.cstate[c].livevolume == 0)) {
                        this->s.keys[lastnotes[c]].release();
                    }
                    std::cout << "i am always here" << std::endl;
                }
                std::cout << "adijp" << std::endl;
                switch(ret.action) {
                case TICK:
                    std::cout << "tick" << std::endl;
                    tick++;
                    break;
                case INC:
                    std::cout << "Row: " << row << std::endl;
                    tick = 0;
                    if(row == 63) {
                        row = 0;
                        r++;
                        goto nextorder;
                    }
                    row++;
                    break;
                case JUMP:
                    std::cout << "Jumping to row=" << ret.location.row
                              << "order=" << ret.location.order << std::endl;
                    tick = 0;
                    r = ret.location.order;
                    row = ret.location.row;
                    goto nextorder;
                }
            }
nextorder:
            continue;
        }
        //std::cout <<
    }*/

    void event_loop() {
        //std::thread t(std::bind(&Keyboard::process_module_notes, this));
        //t.detach();
        while(true) {
            XEvent e;
            XNextEvent(display, &e);
            if(e.type == KeyPress) {
                if(key2note[e.xkey.keycode] == 255) {
                    goto nothing2do;
                }
                XStoreName(display, window, std::to_string(e.xkey.keycode).c_str());
                XClearArea(display, window, 0, 100, 840, 100, True);
                s.keys[key2note[e.xkey.keycode]].reset();
            }
            if(e.type == KeyRelease) {
                if(key2note[e.xkey.keycode] == 255) {
                    goto nothing2do;
                }
                XClearArea(display, window, 0, 100, 840, 100, True);
                s.keys[key2note[e.xkey.keycode]].release();
            }
            if(e.type == Expose) {
                draw_keyboard();
            }
nothing2do:
            XFlush(display);
            XFlushGC(display, gc);
        }
    }
    GC gc;
    int screen;
    PaStream *stream;
    Syntheizer s;
    Display *display;
    Window window;
    XColor Wheat;
    XColor Cornflower_Blue;
    Colormap screen_cmap;
};

static int paCallback( const void *inputBuffer, void *outputBuffer,
                       unsigned long framesPerBuffer,
                       const PaStreamCallbackTimeInfo* timeInfo,
                       PaStreamCallbackFlags statusFlags,
                       void *userData )
{
    float *out = (float*)outputBuffer;
    (void) timeInfo; /* Prevent unused variable warnings. */
    (void) statusFlags;
    (void) inputBuffer;

    ((Keyboard*)userData)->synthesize(out);

    return paContinue;
}

int main()
{
    srand(time(NULL));
    Pa_Initialize();
    Keyboard k;
    k.event_loop();
    return 0;
}
