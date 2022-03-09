#include <iostream>
#include <math.h>
#include <portaudio.h>
#include <thread>
#include <mutex>
#include <functional>
#include <X11/Xlib.h>
#include <cstring>
#include <fstream>
#include <sys/soundcard.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <portmidi.h>
#include <porttime.h>
#include <vector>
#include <queue>
using namespace std;
//import player;

float simplp (float *x, float *y,
              int M, float xm1)
{
    int n;
    y[0] = x[0] + xm1;
    for (n=1; n < M ; n++) {
        y[n] =  x[n]  + x[n-1];
    }
    return x[M-1];
}

struct NoteVoice {
    double volume = 0.0f;
    double pitch;
    double time = 0.0f;
    int pressed = false;
    float velocity = 0.0f;
    float time_released = 0.0f;
    float time_at_release = 0.0f;
    std::queue<float> delayline;
    void reset(float velocity_initial = 1.0f){
        pressed = true;
        time = 0.0f;
        time_at_release = 0.0f;
        time_released = 0.0f;
        //nominally 0.125f at highest
        velocity = velocity_initial/8.0f;
        volume = velocity;
    }
    void release() {
        pressed = false;
        //volume = 0.0f;
    }

    double interval_stack(int stack, double interval, double pitch, double time) {
        double ret = 0.0;
        for(int i = 0; i < stack; i++) {
            double x = pow(2,i+1);
            ret += voice(pitch*x*interval, time)/x;
        }
        return ret;
    }
    double triangle(double ptc) {
        double pt = fmod(ptc, 2 * M_PI);
        if(pt < (M_PI/2.0f)){
            return (2.0f * pt)/M_PI;
        } else if(pt < (3 * (M_PI/2.0f))) {
            return 2.0f - (2.0f * pt)/M_PI;
        } else {
            return -4.0f + (2.0f * pt)/M_PI;
        }
    }

    double sinmod(double ptc) {
        return sqrtf(sinf(ptc)+1)-1;
    }

    double square(double ptc) {
        double pt = fmod(ptc, 2 * M_PI);
        if(pt < M_PI/3.0f) {
            return 1.0f;
        }
        return 0.0f;
    }
    double noise() {
        return ((rand() % 256) - 127)/127.0f;
    }

    double voice(double pitchc, double time) {
        float yeah = noise();
        delayline.push(yeah);
        float front = delayline.front();
        if(delayline.size() > 44100/pitchc) {
            delayline.pop();
            float newcool = (front + delayline.front()) * 0.5f * 0.2f;
            delayline.push(newcool);
            return newcool;
        }
        return yeah;

    }
    // #define CURVE_LINEAR
//#define CURVE_EXP
    double simulate(double samplerate) {
        if(round(volume*16777216) < 1.0f)
            return 0.0f;
        if(!pressed) {
            if(time_at_release  == 0.0f) {
                time_at_release = time;
            }
            time_released = time - time_at_release;
#ifdef CURVE_LINEAR
            volume = (volume - (0.1f/samplerate));
#endif
#ifdef CURVE_EXP
            float ramp = pow(M_E, -time_released);
            volume = velocity*ramp;
#endif
        }
        if(volume < 0.001f){
            volume = 0.0f;
        }
        double pitchc = pitch;
        time += 1.0f/samplerate;
        double sample = voice(pitchc, time)/16.0f;
        //double sample = (voice(pitchc, time)/8.0f + interval_stack(6, 1, pitchc, time)/8.0f + interval_stack(3, 3.0f, pitchc, time)/8.0f) * volume * min(time*10, 1.0);
        return sample;
    }
};

struct Syntheizer {
    struct NoteVoice keys[88];
    Syntheizer(){
        for(int i = 0; i < 88; i++){
            keys[i].pitch = 27.5f  * pow(2.0f, i/12.0f);
        }
    }
    double cframe(double samplerate) {
        double res = 0.0f;
        for(int i = 0; i < 88; i++){
            res += keys[i].simulate(samplerate);        }
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
    Keyboard(int verb_len) {
        reverb_pt = 0;
        for(int i = 0; i < verb_len; i++)
            reverb_buf.push_back(0);
        memset(key2note, 255, 256);
        Pa_OpenDefaultStream(&stream, 0, 1, paFloat32, 44100, 2048, paCallback, this);
        Pa_StartStream(stream);
        Pm_OpenInput(&midistream, Pm_GetDefaultInputDeviceID(), NULL, 64, NULL, NULL);
        display = XOpenDisplay(NULL);
        screen = DefaultScreen(display);
        screen_cmap = DefaultColormap(display, DefaultScreen(display));
        XAutoRepeatOff(display);
        //If something breaks, it's gonna be this hacky sh*t.
        XLookupColor(display, screen_cmap, "Wheat", &Wheat, &Wheat);
        XAllocColor(display, screen_cmap, &Wheat);
        XLookupColor(display, screen_cmap, "Cornflower Blue", &Cornflower_Blue, &Cornflower_Blue);
        XAllocColor(display, screen_cmap, &Cornflower_Blue);
        window = XCreateSimpleWindow(display, RootWindow(display, screen), 10, 10, 840*2, 300, 1,
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
        key2note[56] = 12 + 12;
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
        for(int i = 0; i < 44; i++) {
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
        float o2[2048];
        int verb_len = reverb_buf.size();
        for(int i = 0;i < 2048; i++) {
            reverb_pt = reverb_pt % verb_len;
            o2[i] = s.cframe(44100) + reverb_buf[reverb_pt % verb_len]*0.35f;
            reverb_buf[reverb_pt % verb_len] = o2[i];
            reverb_pt++;
        }
        simplp(o2, obuffer, 2048, o2[0]);
        //mtx.unlock();
    }
    uint16_t period2note[36] = { 856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
                                 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 126,
                                 214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
                               };
    uint8_t key2note[256];

    void process_midi() {
        while(true) {
            PmEvent buffer;
            Pm_Read(midistream, &buffer, 1);
            unsigned char midi[4];
            memset(midi, 0 , 4);
            midi[0] = Pm_MessageStatus(buffer.message);
            midi[1] = Pm_MessageData1(buffer.message);
            midi[2] = Pm_MessageData2(buffer.message);
            switch(midi[0] >> 4) {
                case 0x9:
                    if(midi[2] == 0x00) {
                        s.keys[midi[1]].release();
                        break;
                    }
                    if(!s.keys[midi[1]].pressed)
                        s.keys[midi[1]].reset(midi[2]/127.0f);
                    break;
                case 0x8:
                    s.keys[midi[1]].release();
                    break;
            }
        }
    }

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
        std::thread t(std::bind(&Keyboard::process_midi, this));
        t.detach();
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
    int reverb_pt;
    std::vector<float> reverb_buf;
    GC gc;
    int screen;
    PaStream *stream;
    PmStream *midistream;
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
    Pm_Initialize();
    Pt_Start(1, NULL, NULL);
    srand(time(NULL));
    Pa_Initialize();
    Keyboard k(1);
    k.event_loop();
    return 0;
}
