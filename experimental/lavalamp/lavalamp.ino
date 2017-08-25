#include "LPD8806.h"
#include "SPI.h"
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


// Set the first variable to the NUMBER of pixels.
// The LED strips are 32 LEDs per meter but you can extend/cut the strip
LPD8806 strip = LPD8806(N_LEDS, dataPin, clockPin);


// Allocate two pixel buffers (for double buffering)
uint32_t buf_1 [N_LEDS];
uint32_t buf_2 [N_LEDS];

uint32_t *state = buf_1;
uint32_t *next_state = buf_2;


void setup() {
    // This is for Trinket 5V 16MHz, you can remove these three lines if you are not using a Trinket
#if defined (__AVR_ATtiny85__)
    if (F_CPU == 16000000) clock_prescale_set(clock_div_1);
#endif
    // End of trinket special code

    init_lamp();

    strip.begin();
    strip.show(); // Initialize all pixels to 'off'
}


void loop() {
    update_lamp();
    render_lamp();
    strip.show();
}


void init_lamp() {
    for (int i = 0; i < N_LEDS; i++) {
        // On or off
        if (random(1000) < 50) {
            state[i] = random_rgb_color();
        }
    }
}

void update_lamp() {
    for (int u = 0; u < 50; u++) {
        int i = random(N_LEDS);
        
        uint32_t avg_color = combineColor(
            combineColor(state[(i-2) % N_LEDS], state[(i-3) % N_LEDS], 0.5),
            combineColor(state[(i+2) % N_LEDS], state[(i+3) % N_LEDS], 0.5),
            0.5
        );
        next_state[i] = dimColor(avg_color, 0.99);

        // Randomize births
        if (random(1000) < 10) {
            next_state[i] = random_rgb_color();
        }

    }
}

void render_lamp() {
    for (int i = 0; i < N_LEDS; i++) {
        strip.setPixelColor(i, combineColor(state[i], next_state[i], 0.5));
    }

    uint32_t *tmp = state;
    state = next_state;
    next_state = tmp;
}

// Random full-spectrum RGB color
uint32_t random_rgb_color() {
    return strip.Color((byte)(random() % 128), (byte)(random() % 128), (byte)(random() % 128));
}

uint32_t dimColor(uint32_t c, float fraction) {
    byte  r, g, b;
    g = ((c >> 16) & 0x7f) * fraction;
    r = ((c >>  8) & 0x7f) * fraction;
    b =  (c        & 0x7f) * fraction;
    return strip.Color(r, g, b);
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
