#include "LPD8806.h"
#include "SPI.h"
#include <EEPROM.h>
#include <elapsedMillis.h>


#ifdef __AVR__
#include <avr/power.h>
#endif

#if defined(USB_SERIAL) || defined(USB_SERIAL_ADAFRUIT)
// this is for teensyduino support
int dataPin = 2;
int clockPin = 1;
#else 
// these are the pins we use for the LED belt kit using
// the Leonardo pinouts
int dataPin = 16;
int clockPin = 15;
#endif

#define N_LEDS       160

#define MODEADDRESS 500 // Byte address in EEPROM to use for the mode counter (0-1023).u
#define SWITCHADDRESS 501 // Byte address in EEPROM to use for the mode switch flag (0-1023).
#define MODE_SWITCH_TIME 2000 // Number of milliseconds before power cycle doesn't change modes

elapsedMillis timer;
elapsedMillis step_timer;

#define SWITCH_MODES 1
#define DONT_SWITCH_MODES 0

// Set the first variable to the NUMBER of pixels.
// The LED strips are 32 LEDs per meter but you can extend/cut the strip
LPD8806 strip = LPD8806(N_LEDS, dataPin, clockPin);

// Configs below are mainly intended to be used in indirect rendering mode

// Actor kinds
// enum kind {sparkle, slow_sparkle, spiral, glimmer, shimmer, bee, sparkler, lavalamp};
enum kind {sparkle, slow_sparkle, spiral, glimmer, shimmer, bee, sparkler};
enum palette {full_rgb, white, palette_1, palette_2, palette_3, palette_4, palette_5};
enum background {keep, clear, fade, shimmer_fade, double_buffer};

// NOTE: don't add to this struct without compacting it; also: enums are int,
// so use sparingly
#define MAX_ACTORS 30
typedef struct actor {
    enum kind kind;
    enum palette palette;
    uint32_t color;
    uint8_t pos;
    uint16_t duration;
    uint16_t counter;
    byte length;
    byte speed;
    float rate;
} actor;
actor actors[MAX_ACTORS];
int n_actors = MAX_ACTORS;


// Soph-palette
#define N_PALETTE_1_COLORS 5
uint32_t palette_1_colors[] = {
    strip.Color(92/2, 255/2, 0),     // 5C00FF (92,0,255)
    strip.Color(65/2, 111/2, 255/2), // 41FF6F (65,255,111)
    strip.Color(253/2, 24/2, 255/2), // #FDFF18 (253,255,24)
    strip.Color(239/2, 187/2, 24/2), // #EF18BB (239,24,187)
    strip.Color(0/2, 71/2, 0/2), // #000047 (0,0,71)
};


// Simpler palette
#define N_PALETTE_2_COLORS 3
uint32_t palette_2_colors[] = {
    strip.Color(127, 127, 0), 
    strip.Color(0, 127, 127),
    strip.Color(64, 127, 0),
};

// Fire
#define N_PALETTE_3_COLORS 3
uint32_t palette_3_colors[] = {
    strip.Color(161/2, 0, 0), 
    strip.Color(234/2, 0, 35/2),
    strip.Color(255/2, 0, 129/2),
};

// Moon
#define N_PALETTE_4_COLORS 3
uint32_t palette_4_colors[] = {
    strip.Color(207/2, 109/2, 165/2), 
    strip.Color(207/2, 230/2, 221/2),
    strip.Color(255/2, 255/2, 255/2),
};

// Original palette from Jeff
#define N_PALETTE_5_COLORS 7
uint32_t palette_5_colors[] = {
    strip.Color(127, 0, 0),
    strip.Color(127, 127, 0),
    strip.Color(0, 127, 0),
    strip.Color(0, 127, 127),
    strip.Color(0, 0, 127),
    strip.Color(127, 0, 127),
    strip.Color(127, 127, 127)
};

// Used in direct mode
#define N_COLORS N_PALETTE_5_COLORS
uint32_t* colors = palette_5_colors;

// Allocate two pixel buffers (for double buffering)
/*
 *uint32_t buf_1 [N_LEDS];
 *uint32_t buf_2 [N_LEDS];
 *
 *uint32_t *state = buf_1;
 *uint32_t *next_state = buf_2;
 */

uint32_t background_color = strip.Color(0, 0, 0);
float background_fade_rate = 0.1;
enum background background = clear;

// NOTE: modevalue=0 is reserved for direct rendering mode
#define NUMBEROFMODES 8
uint8_t modevalue = 0;


void setup() {
    // This is for Trinket 5V 16MHz, you can remove these three lines if you are not using a Trinket
#if defined (__AVR_ATtiny85__)
    if (F_CPU == 16000000) clock_prescale_set(clock_div_1);
#endif
    // End of trinket special code

    // Grab the modevalue from EEPROM
    read_mode_state();

    // modevalue == 0 is direct rendering mode (cede all control over to rendering fn)
    if (modevalue > 0) {
        // Setup the scene (our config)
        scene_init();

        // Set up the actors we requested
        actors_init();
    }

    strip.begin();
    strip.show(); // Initialize all pixels to 'off'
}

void read_mode_state() {
    boolean modeSwitch = EEPROM.read(SWITCHADDRESS); // read mode switch from EEPROM to decide whether to switch modes

    EEPROM.write(SWITCHADDRESS, SWITCH_MODES); // write to EEPROM so that we will switch modes next time (if power is interrupted before we change this)
    timer = 0; // when this counter reaches MODE_SWITCH_TIME, we will change the mode switch so that we don't switch modes

    modevalue = EEPROM.read(MODEADDRESS); // read mode counter from EEPROM (non-volatile memory)

    if (modevalue >= NUMBEROFMODES) modevalue = NUMBEROFMODES; // fix out-of-range mode counter (happens when the program is run for the first time)
    if (modeSwitch == SWITCH_MODES) {
        if (++modevalue >= NUMBEROFMODES) modevalue = 0; // increment the mode counter
        EEPROM.write(MODEADDRESS, modevalue);
    }
}

void loop() {
    // modevalue == 0 is direct rendering mode (cede all control over to rendering fn)
    if (modevalue > 0) {
        indirect_mode_loop();
    } else {
        direct_mode_loop();
    }
}

void indirect_mode_loop() {
    // Make changes to the scene if necessary
    scene_update();

    // Perform all the actor state updates
    actors_update();

    // Clear the canvas and render all the actors
    actors_render();

    strip.show();
    // delay(random() % 20);
    // delay(100);
}

void direct_mode_loop() {
    for (int i = 1; i < N_COLORS; i++) {
        candyCane(colors[i], colors[(i - 1) % N_COLORS], 3, 7, 100);
    }
    for (int j = 0; j < 10; j++) {
        for (int i = 1; i <= N_COLORS; i++ ) {
            merge(colors[i % N_COLORS], (j + i) % 2, 2);
        }
    }
    stack(colors[4], 0, 1, 5);
    for (int i = 0; i < N_COLORS - 2; i++) {
        stack(colors[i], colors[(i - 1) % N_COLORS], i % 2, 0);
    }
    rainbowCycleWave(0);
}

///////////////////////////////////////////////////////////////////////////////
// Indirect rendering mode stuff
void scene_init() {
    switch (modevalue-1) {
      case 0: scene_8_init(); break;
      case 1: scene_9_init(); break;
      case 2: scene_10_init(); break;
      case 3: scene_11_init(); break;
      case 4: scene_12_init(); break;
      case 5: scene_13_init(); break;
      // case 6: scene_14_init(); break;
    }
}

void scene_update() {
    switch (modevalue-1) {
        // NOTE: scenes prior to 8 didn't have updates
        case 0: scene_8_update(); break;
        case 1: scene_9_update(); break;
        case 2: scene_10_update(); break;
        case 3: scene_11_update(); break;
        case 4: scene_12_update(); break;
        case 5: scene_13_update(); break;
        // case 6: scene_14_update(); break;
    }
}


void scene_1_init() {
    // Sparklers 
    n_actors = 20;
    background = fade;
    background_fade_rate = 0.5;
    for (int a = 0; a < n_actors; a++) {
        actors[a].kind = sparkler;
        actors[a].palette = white;
        actors[a].rate = 0.3;
        actors[a].speed = 1;
    }
}

void scene_2_init() {
    // Bees!
    n_actors = MAX_ACTORS;
    background_color = dimColor(palette_1_colors[0], 0.01);
    for (int a = 0; a < n_actors; a++) {
        actors[a].kind = bee;
        actors[a].palette = full_rgb;
        // Update X / 50 frames
        actors[a].speed = random(5) + 1;
    }
}

void scene_3_init() {
    // All fast sparkles
    n_actors = 10;
    for (int a = 0; a < n_actors; a++) {
        actors[a].kind = sparkle;
        actors[a].palette = full_rgb;
    }
}

void scene_4_init() {
    // Slower sparkles; can control the strobe effect by setting longer durations
    n_actors = MAX_ACTORS;
    for (int a = 0; a < n_actors; a++) {
        actors[a].kind = slow_sparkle;
        actors[a].palette = white;
        actors[a].duration = random() % 20;
    }
}

void scene_5_init() {
    // Spirals and sparkles!
    n_actors = 20;
    for (int a = 0; a < 10; a++) {
        actors[a].kind = spiral;
        actors[a].palette = full_rgb;
        actors[a].length = 3 + random() % 3;
        actors[a].speed = random() % 2 + 1;
        actors[a].counter = 1;  // counter is the number of pixels to skip
        actors[a].rate = 1.0; // update rate
    }
    for (int a = 10; a < n_actors; a++) {
        actors[a].kind = slow_sparkle;
        actors[a].palette = white;
        actors[a].duration = random() % 20;
    }
}

void scene_6_init() {
    // Spirals on a background with optional shimmmer
    n_actors = 20;
    background_color = dimColor(palette_1_colors[0], 0.01);
    for (int a = 0; a < n_actors-2; a++) {
        actors[a].kind = spiral;
        actors[a].palette = palette_1;
        actors[a].length = 3 + random() % 3;
        actors[a].speed = random() % 2 + 1;
        actors[a].counter = 1;  // counter is the number of pixels to skip
        actors[a].rate = 1.0; // update rate
    }
    // actors[n_actors-1].kind = shimmer;
    actors[n_actors-1].kind = sparkle;
}

void scene_7_init() {
    // Fading bees
    n_actors = 20;
    background = fade;
    background_fade_rate = 0.01;
    for (int a = 0; a < n_actors; a++) {
        actors[a].kind = bee;
        actors[a].palette = palette_2;
        actors[a].speed = 40; // Update speed / 50 frames
    }
}

void scene_8_init() {
    // Upward spirals with randomizing colors and a few persistent bees
    // Disco princess
    n_actors = 20;
    background = fade;
    background_fade_rate = 0.1;
    for (int a = 0; a < n_actors - 1; a++) {
        if (a >= 5) {
            actors[a].kind = bee;
            actors[a].palette = palette_1;
            actors[a].speed = 40; // Update speed / 50 frames

        } else {
            actors[a].kind = spiral;
            actors[a].palette = palette_2;
            actors[a].length = 5;
            actors[a].speed = 3;
            actors[a].counter = 1;  // counter is the number of pixels to skip
            actors[a].rate = 1.0; // update rate
        }
    }
    actors[n_actors-1].kind = shimmer;
}

void scene_8_update() {
    // Randomize the spiral colors
    for (int a = 0; a < n_actors - 1; a++) {
        if (a < 10) {
            if (random(1000) < 100) {
                actors[a].color = random_palette_color(actors[a].palette);
            }
        }
    }
}

void scene_9_init() {
    // Very princess-y
    n_actors = 30;
    background = shimmer_fade;
    background_fade_rate = 0.3;
    for (int a = 0; a < n_actors; a++) {
        if (a >= 5) {
            actors[a].kind = bee;
            actors[a].palette = white;
            actors[a].speed = 40; // Update speed / 50 frames
            // TODO: speed up sparklers
            /*
             *actors[a].kind = sparkler;
             *actors[a].palette = white;
             *actors[a].rate = 0.1;
             */

        } else {
            actors[a].kind = spiral;
            actors[a].palette = palette_2;
            actors[a].length = 5;
            actors[a].speed = 3;
            actors[a].counter = 1;  // counter is the number of pixels to skip
            actors[a].rate = 1.0; // update rate
        }
    }
}

void scene_9_update() {
    // Randomize the spiral colors
    for (int a = 0; a < n_actors; a++) {
        if (a < 10) {
            if (random(1000) < 800) {
                actors[a].color = random_palette_color(actors[a].palette);
            }
        }
    }
}

void scene_10_init() {
    n_actors = 20;
    background = shimmer_fade;
    background_fade_rate = 0.7;
    for (int a = 0; a < n_actors; a++) {
        if (a >= 10) {
            actors[a].kind = sparkle;
            actors[a].palette = palette_4;
        } else if (a >= 5) {
            actors[a].kind = sparkle;
            actors[a].palette = white;
        } else {
            actors[a].kind = spiral;
            actors[a].palette = palette_4;
            actors[a].length = 5;
            actors[a].speed = 3;
            actors[a].counter = 1;  // counter is the number of pixels to skip
            actors[a].rate = 1.0; // update rate
        }
    }
}

void scene_10_update() {
    // Randomize the spiral colors
    for (int a = 0; a < n_actors; a++) {
        if (a < 5) {
            if (random(1000) < 800) {
                actors[a].color = random_palette_color(actors[a].palette);
            }
        }
    }
}

void scene_11_init() {
    // Much more strob-y fire
    n_actors = 20;
    // Remove this and it gets even stronger
    background = fade;
    background_fade_rate = 0.7;
    for (int a = 0; a < n_actors; a++) {
        if (a >= 15) {
            actors[a].kind = sparkle;
            actors[a].palette = palette_4;
        } else if (a >= 10) {
            actors[a].kind = sparkle;
            actors[a].palette = white;
        } else {
            actors[a].kind = spiral;
            actors[a].palette = palette_4;
            actors[a].length = 5;
            actors[a].speed = 3;
            actors[a].counter = 1;  // counter is the number of pixels to skip
            actors[a].rate = 1.0; // update rate
        }
    }
}

void scene_11_update() {
    // Randomize the spiral colors
    for (int a = 0; a < n_actors; a++) {
        if (a < 5) {
            if (random(1000) < 800) {
                actors[a].color = random_palette_color(actors[a].palette);
            }
        }
    }
}

void scene_rain_init(enum palette palette) {
    // White rain (palette 4) / pink/cyan rain (palette 2)
    n_actors = 12;
    background = fade;
    background_fade_rate = 0.05;
    for (int a = 0; a < n_actors; a++) {
        actors[a].palette = palette;
        if (a >= 12) {
            actors[a].kind = sparkle;
        } else {
            int b = n_actors - a;
            actors[a].kind = spiral;
            actors[a].length = random(4) + 1;
            actors[a].speed = -1;
            if (random(1000) < 900) {
                actors[a].counter = 5;  // counter is the number of pixels to skip, 5 means vertical
                actors[a].rate = 1.0 / (b + 1.0); // update rate
            } else {
                actors[a].counter = 1;
                actors[a].rate = 1.0; // update rate
            }
        }
    }
}

void scene_rain_update() {
    for (int a = 0; a < n_actors; a++) {
        if (random(1000) < 50) {
            actors[a].color = random_palette_color(actors[a].palette);
            actors[a].length = min(5, max(1, actors[a].length + (random(3) - 1)));
            actors[a].rate = random(500) / 1000.0;
        }
    }
}

void scene_12_init() {
    scene_rain_init(palette_2);
}

void scene_12_update() {
    scene_rain_update();
}

void scene_13_init() {
    scene_rain_init(palette_4);
}

void scene_13_update() {
    scene_rain_update();
}

/*
 *void scene_14_init() {
 *    n_actors = 1;
 *    background = double_buffer;
 *    for (int a = 0; a < n_actors; a++) {
 *        actors[a].palette = palette_2;
 *        actors[a].kind = lavalamp;
 *    }
 *}
 *
 *void scene_14_update() {
 *}
 */

void actors_init() {
    for (int a = 0; a < n_actors; a++) {
        switch (actors[a].kind) {
            case       sparkle: sparkle_init(actors[a]);       break;
            case  slow_sparkle: slow_sparkle_init(actors[a]);  break;
            case        spiral: spiral_init(actors[a]);        break;
            case       glimmer: glimmer_init(actors[a]);       break;
            case       shimmer: shimmer_init(actors[a]);       break;
            case           bee: bee_init(actors[a]);           break;
            case      sparkler: sparkler_init(actors[a]);      break;
            // case      lavalamp: lavalamp_init(actors[a]);      break;
        }
    }
}

void actors_update() {
    for (int a = 0; a < n_actors; a++) {
        switch (actors[a].kind) {
            case       sparkle: sparkle_update(actors[a]);       break;
            case  slow_sparkle: slow_sparkle_update(actors[a]);  break;
            case        spiral: spiral_update(actors[a]);        break;
            case       glimmer: glimmer_update(actors[a]);       break;
            case       shimmer: shimmer_update(actors[a]);       break;
            case           bee: bee_update(actors[a]);           break;
            case      sparkler: sparkler_update(actors[a]);      break;
            // case      lavalamp: lavalamp_update(actors[a]);      break;
        }
    }
}

void actors_render() {
    // Black out the array
    if (background == clear) {
        for (int i = 0; i < N_LEDS; i++) {
            strip.setPixelColor(i, background_color);
        }
    } else if (background == fade) {
        for (int i = 0; i < N_LEDS; i++) {
            // TODO: fade to background color, not black
            strip.setPixelColor(i, dimColor(strip.getPixelColor(i), 1.0 - background_fade_rate));
        }
    } else if (background == shimmer_fade) {
        for (int i = 0; i < N_LEDS; i++) {
            // TODO: fade to background color, not black
            strip.setPixelColor(i, dimColor(strip.getPixelColor(i), 1.0 - background_fade_rate * random(1000) / 1000.0));
        }
    } else if (background == double_buffer) {
/*
 *        for (int i = 0; i < N_LEDS; i++) {
 *            strip.setPixelColor(i, combineColor(state[i], next_state[i], 0.5));
 *        }
 *
 *        uint32_t *tmp = state;
 *        state = next_state;
 *        next_state = tmp;
 */
    }

    // Render the actors
    for (int a = 0; a < n_actors; a++) {
        switch (actors[a].kind) {
            case       sparkle: sparkle_render(actors[a]);       break;
            case  slow_sparkle: slow_sparkle_render(actors[a]);  break;
            case        spiral: spiral_render(actors[a]);        break;
            case       glimmer: glimmer_render(actors[a]);       break;
            case       shimmer: shimmer_render(actors[a]);       break;
            case           bee: bee_render(actors[a]);           break;
            case      sparkler: sparkler_render(actors[a]);      break;
            // case      lavalamp: lavalamp_render(actors[a]);      break;
        }
    }
}

/* Actor implementations */

void sparkle_init(actor& a) {
}

void sparkle_update(actor& a) {
}

void sparkle_render(actor& a) {
    strip.setPixelColor(random() % N_LEDS, random_palette_color(a.palette));
}

void slow_sparkle_init(actor& a) {
    a.pos = random(N_LEDS);
    a.color = random_palette_color(a.palette);
    a.counter = a.duration;
}

void slow_sparkle_update(actor& a) {
    // a.pos = random() % N_LEDS;
    // Dim by 1/duration each iter
    a.color = dimColor(a.color, 1.0 - 1.0 / (float)a.duration);

    if (--a.counter == 0) {
        slow_sparkle_init(a);
    }
}

void slow_sparkle_render(actor& a) {
    strip.setPixelColor(a.pos, a.color);
}

void spiral_init(actor& a) {
    a.pos = random(N_LEDS);
    a.color = random_palette_color(a.palette);
}

void spiral_update(actor& a) {
    if (a.rate == 1.0 || random(1000) < 1000 * a.rate) {
        a.pos = (a.pos + a.speed * a.counter) % N_LEDS;
    }
}

void spiral_render(actor& a) {
    uint32_t c = a.color;
    for (int i = 0; i < a.length; i++) {
        if (i == 0) {
            strip.setPixelColor((a.pos + i * a.counter) % N_LEDS, dimColor(c, 0.1));
        } else {
            strip.setPixelColor((a.pos + i * a.counter) % N_LEDS, c);
        }
        c = dimColor(c, 1.0 - 1.0 / a.length);
    }
}

void glimmer_init(actor& a) {
}

void glimmer_update(actor& a) {
}

void glimmer_render(actor& a) {
    for (int i = 0; i < N_LEDS; i++) {
        uint32_t c = strip.getPixelColor(i);
        strip.setPixelColor(i, dimColor(c, random(900) / 1000.0));
    }

}

void shimmer_init(actor& a) {
}

void shimmer_update(actor& a) {
}

void shimmer_render(actor& a) {
    for (int i = 0; i < N_LEDS; i++) {
        uint32_t c = strip.getPixelColor(i);
        strip.setPixelColor(i, dimColor(c, 0.5 * random(1000) / 1000.0) + 0.5);
    }
}

void bee_init(actor& a) {
    a.pos = random() % N_LEDS;
    a.color = random_palette_color(a.palette);
    // Will use counter to mean refactory period
    a.counter = 0;
}

void bee_update(actor& a) {
    // Bees don't move sometimes
    if (a.counter == 0 && random(50) < a.speed) {
        // If you only do +/- 1, you move around the pole, looks more like a
        // sparkle; +/2 2 or 4 means that things will move more or less to nearby
        // pixels you can see from your viewing angle.
        a.pos = (a.pos + 2 * (random(5) - 2)) % N_LEDS;
        a.counter = 3;
    }

    if (a.counter > 0) {
        --a.counter;
    }
}

void bee_render(actor& a) {
    // strip.setPixelColor(a.pos, a.color);
    strip.setPixelColor(a.pos, dimColorRandomBase(a.color, 750)); 
}

void sparkler_init(actor& a) {
    a.pos = random() % N_LEDS;
    a.color = random_palette_color(a.palette);
}

void sparkler_update(actor& a) {
    if (random(1000) < 100) {
        a.pos = (a.pos + a.speed) % N_LEDS;
    }
}

void sparkler_render(actor& a) {
    strip.setPixelColor(a.pos, dimColorRandomBase(a.color, 750)); 

    // Pole geometry: in order to look like something is centered around a
    // point on the pole, you need to wrap. Points that look nearby are:
    // +/- 2
    // +/- 3
    // +/- 5 (+/- 1 in y direction)
    // +/- 7
    // +/- 8
    // +/- 10 (+/- 2 in y direction)

    for (int i = -10; i <= 10; i++) {
        uint32_t c;
        if (random(1000) < a.rate * 1000) {
            switch (i) {
                case -10: 
                case  10: 
                    if (random(10) == 0) {
                        c = dimColorRandomBase(dimColor(a.color, 1/10.), 0);
                        strip.setPixelColor((a.pos + i) % N_LEDS, c);
                    }
                    break;
                case -8: 
                case  8: 
                    if (random(8) == 0) {
                        c = dimColorRandomBase(dimColor(a.color, 1/8.), 100);
                        strip.setPixelColor((a.pos + i) % N_LEDS, c);
                    }
                    break;
                case -7: 
                case  7: 
                    if (random(7) == 0) {
                        c = dimColorRandomBase(dimColor(a.color, 1/7.), 200);
                        strip.setPixelColor((a.pos + i) % N_LEDS, c);
                    }
                    break;
                case -5: 
                case  5: 
                    if (random(5) == 0) {
                        c = dimColorRandomBase(dimColor(a.color, 1/5.), 400);
                        strip.setPixelColor((a.pos + i) % N_LEDS, c);
                    }
                    break;
                case -3: 
                case  3: 
                case -2: 
                case  2: 
                    if (random(2) == 0) {
                        c = dimColorRandomBase(a.color, 600);
                        strip.setPixelColor((a.pos + i) % N_LEDS, c);
                    }
                    break;
            }
        }

    }

}


/*
 *void lavalamp_init(actor& a) {
 *    for (int i = 0; i < N_LEDS; i++) {
 *        // On or off
 *        if (random(1000) < 50) {
 *            state[i] = random_palette_color(a.palette);
 *        }
 *    }
 *}
 *
 *void lavalamp_update(actor& a) {
 *    // TODO: configurable
 *    for (int u = 0; u < 50; u++) {
 *        int i = random(N_LEDS);
 *        
 *        uint32_t avg_color = combineColor(
 *            combineColor(state[(i-2) % N_LEDS], state[(i-3) % N_LEDS], 0.5),
 *            combineColor(state[(i+2) % N_LEDS], state[(i+3) % N_LEDS], 0.5),
 *            0.5
 *        );
 *        // TODO: configurable
 *        next_state[i] = dimColor(avg_color, 0.99);
 *
 *        // Randomize births
 *        // TODO: configurable
 *        if (random(1000) < 10) {
 *            next_state[i] = random_rgb_color();
 *        }
 *    }
 *}
 *
 *void lavalamp_render(actor& a) {
 *    // Handled by double_buffer
 *}
 */

///////////////////////////////////////////////////////////////////////////////
// Direct mode rendered patterns                  
void rainbowCycleWave(uint8_t wait) {
    uint16_t i, j;

    for (j=0; j < 384 * 3; j++) {     // 5 cycles of all 384 colors in the wheel
        for (i=0; i < N_LEDS; i++) {
            // tricky math! we use each pixel as a fraction of the full 384-color
            // wheel (thats the i / strip.numPixels() part)
            // Then add in j which makes the colors go around per pixel
            // the % 384 is to make the wheel cycle around
            strip.setPixelColor(i, Wheel(((i * 384 / N_LEDS) + j) % 384));
        }
        strip.show();   // write all the pixels out
        delay(wait);
    }
}

// An "ordered dither" fills every pixel in a sequence that looks
// sparkly and almost random, but actually follows a specific order.
void dither(uint8_t wait) {
    uint32_t rainbowColors[N_LEDS];  // Make the dither pattern transition into the rainbowCycleWave pattern
    for (int i=0; i < N_LEDS; i++) {
        rainbowColors[i] = Wheel(((i * 384 / N_LEDS)) % 384);
    }

    // Determine highest bit needed to represent pixel index
    int hiBit = 0;
    int n = strip.numPixels() - 1;
    for(int bit=1; bit < 0x8000; bit <<= 1) {
        if(n & bit) hiBit = bit;
    }

    int bit, reverse;
    for(int i=0; i<(hiBit << 1); i++) {
        // Reverse the bits in i to create ordered dither:
        reverse = 0;
        for(bit=1; bit <= hiBit; bit <<= 1) {
            reverse <<= 1;
            if(i & bit) reverse |= 1;
        }
        strip.setPixelColor(reverse, rainbowColors[reverse]);
        strip.show();
        delay(wait);
    }
    delay(250); // Hold image for 1/4 sec
}

// Create a candy cane pattern going down the strip
void candyCane(uint32_t c1, uint32_t c2, uint8_t len, uint8_t space, uint8_t wait) {
    uint32_t pixelColor;

    for (int i = 0; i < N_LEDS; i++) {
        int location = 0;
        while (location < N_LEDS) {
            for (int j = 0; j < len; j++, location++) {
                if (i < location) {
                    continue;
                }
                strip.setPixelColor((i + location) % N_LEDS, c1);
            }
            for (int j = 0; j < space; j++, location++) {
                if (i < location) {
                    continue;
                }
                strip.setPixelColor((i + location) % N_LEDS, c2);
            }
        }
        strip.show();
        delay(wait);
    } 
}

// Lights merge together either from top and bottom or apart from middle
void merge(uint32_t c1, boolean fromEdges, uint8_t wait) {

    for (int i = 0; i < N_LEDS / 2 - 1; i++) {
        for (int k = 0; k <= i; k++) {
            uint32_t dimmedColor = dimColor(c1, (1.0 + k) / (1.0 + i));
            if (fromEdges) {
                strip.setPixelColor(k, dimmedColor);
                strip.setPixelColor(N_LEDS - 1 - k, dimmedColor);
            } else {
                strip.setPixelColor(N_LEDS / 2 - 1 - k, dimmedColor);
                strip.setPixelColor(N_LEDS / 2 + k, dimmedColor);
            }
        }
        strip.show();
        delay(wait); 
    }
}

// Sine wave effect
#define PI 3.14159265
void wave(uint32_t c, int cycles, uint8_t wait) {
    float y;
    byte  r, g, b, r2, g2, b2;

    // Need to decompose color into its r, g, b elements
    g = (c >> 16) & 0x7f;
    r = (c >>  8) & 0x7f;
    b =  c        & 0x7f; 

    for(int x=0; x<(strip.numPixels()*5); x++)
    {
        for(int i=0; i<strip.numPixels(); i++) {
            y = sin(PI * (float)cycles * (float)(x + i) / (float)strip.numPixels());
            if(y >= 0.0) {
                // Peaks of sine wave are white
                y  = 1.0 - y; // Translate Y to 0.0 (top) to 1.0 (center)
                r2 = 127 - (byte)((float)(127 - r) * y);
                g2 = 127 - (byte)((float)(127 - g) * y);
                b2 = 127 - (byte)((float)(127 - b) * y);
            } else {
                // Troughs of sine wave are black
                y += 1.0; // Translate Y to 0.0 (bottom) to 1.0 (center)
                r2 = (byte)((float)r * y);
                g2 = (byte)((float)g * y);
                b2 = (byte)((float)b * y);
            }
            strip.setPixelColor(i, r2, g2, b2);
        }
        strip.show();
        delay(wait);
    }
}

// Create a stack of colors in either up or down direction
void stack(uint32_t c1, uint32_t c2, boolean downDirection, uint8_t wait) {
    for (int i = 0; i < strip.numPixels(); i++) {
        strip.setPixelColor(i, c2);
    }
    if (downDirection) {
        strip.setPixelColor(0, c1); // Set the first row to the color
        strip.show();
        delay(wait);
        for (int max = N_LEDS; max > 0; max--) {
            for (int i = 1; i < max; i++) { // Move the colored row down
                strip.setPixelColor(i, c1); // Move the row down
                strip.setPixelColor(i-1, c2); // Clear the previous row
            }
            strip.show();
            delay(wait);
        }
    } else {
        strip.setPixelColor(N_LEDS-1, c1); // Set the last row to the color
        strip.show();
        delay(wait);
        for (int min = 0; min < N_LEDS; min++) {

            for (int i = N_LEDS - 2; i >= min; i--) { // Move the colored row up
                strip.setPixelColor(i, c1); // Move the row down
                strip.setPixelColor(i+1, c2); // Clear the previous row
            }
            strip.show();
            delay(wait);
        }
    }    
}

/* Helper functions */

//Input a value 0 to 384 to get a color value.
//The colours are a transition r - g - b - back to r

uint32_t Wheel(uint16_t WheelPos)
{
    byte r, g, b;
    switch(WheelPos / 128)
    {
        case 0:
            r = 127 - WheelPos % 128; // red down
            g = WheelPos % 128;       // green up
            b = 0;                    // blue off
            break;
        case 1:
            g = 127 - WheelPos % 128; // green down
            b = WheelPos % 128;       // blue up
            r = 0;                    // red off
            break;
        case 2:
            b = 127 - WheelPos % 128; // blue down
            r = WheelPos % 128;       // red up
            g = 0;                    // green off
            break;
    }
    return(strip.Color(r,g,b));
}

// Random full-spectrum RGB color
uint32_t random_rgb_color() {
    return strip.Color((byte)(random() % 128), (byte)(random() % 128), (byte)(random() % 128));
}

// Random white color
uint32_t random_white_color() {
    byte basis = random() % 128;
    return strip.Color(basis, basis, basis);
}

uint32_t random_palette_color(palette p) {
    switch (p) {
        case full_rgb:  return random_rgb_color();
        case white:     return random_white_color();
        case palette_1: return palette_1_colors[random() % N_PALETTE_1_COLORS];
        case palette_2: return palette_2_colors[random() % N_PALETTE_2_COLORS];
        case palette_3: return palette_3_colors[random() % N_PALETTE_3_COLORS];
        case palette_4: return palette_4_colors[random() % N_PALETTE_4_COLORS];
    }
}

void unpackColor(uint32_t c, byte* r, byte* g, byte* b) {
    *g = ((c >> 16) & 0x7f);
    *r = ((c >>  8) & 0x7f);
    *b =  (c        & 0x7f);
}


uint32_t dimColor(uint32_t c, float fraction) {
    byte  r, g, b;
    g = ((c >> 16) & 0x7f) * fraction;
    r = ((c >>  8) & 0x7f) * fraction;
    b =  (c        & 0x7f) * fraction;
    return strip.Color(r, g, b);
}

uint32_t dimColorRandomBase(uint32_t c, int base) {
    return dimColor(c, (base + random(1000-base)) / 1000.0);
}

uint32_t combineColor(uint32_t c1, uint32_t c2, float rate) {
    byte  r1, g1, b1;
    g1 = ((c1 >> 16) & 0x7f) * rate;
    r1 = ((c1 >>  8) & 0x7f) * rate;
    b1 =  (c1        & 0x7f) * rate;

    byte  r2, g2, b2;
    g2 = ((c2 >> 16) & 0x7f) * (1.0 - rate);
    r2 = ((c2 >>  8) & 0x7f) * (1.0 - rate);
    b2 =  (c2        & 0x7f) * (1.0 - rate);
    return strip.Color(r1 + r2, g1 + g2, b1 + b2);
}
