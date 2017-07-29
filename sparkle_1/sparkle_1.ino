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
// TODO: one of these could just be the light state itself
uint32_t buf_1 [N_LEDS];
uint32_t buf_2 [N_LEDS];

// Set up points to the pixel buffers
uint32_t *pixels = buf_1;
uint32_t *next_pixels = buf_2;

// #define N_COLORS 2
// uint32_t colors[] = { strip.Color(127, 0, 0), strip.Color(64, 127, 0) };

// 


#define N_COLORS 5
uint32_t colors[] = {
  strip.Color(92/2, 255/2, 0),     // 5C00FF (92,0,255)
  strip.Color(65/2, 111/2, 255/2), // 41FF6F (65,255,111)
  strip.Color(253/2, 24/2, 255/2), // #FDFF18 (253,255,24)
  strip.Color(239/2, 187/2, 24/2), // #EF18BB (239,24,187)
  strip.Color(0/2, 71/2, 0/2), // #000047 (0,0,71)
};



                  
void setup() {
  // This is for Trinket 5V 16MHz, you can remove these three lines if you are not using a Trinket
  #if defined (__AVR_ATtiny85__)
    if (F_CPU == 16000000) clock_prescale_set(clock_div_1);
  #endif
  // End of trinket special code

  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  next_off();
}


void loop() {
  // Sophie: save that mode
  drops(1, 1);
  // drops(2, 0.01);

  // This is painful
  sparkle(10);
  delay(100);
}

void sparkle(int sparkles) {
  next_off();
  next_random_sparkle(sparkles);
  blit();
  strip.show();
}


void next_spread(bool down) {
  for (int i = 0; i < N_LEDS; i++) { 
    byte rr, rg, rb;

    if (down) {
      uint32_t rc = pixels[(i+1) % N_LEDS];
      unpackColor(rc, &rr, &rg, &rb);
    } else {
      // Up
      uint32_t rc = pixels[(i-1) % N_LEDS];
      unpackColor(rc, &rr, &rg, &rb);
    }

    uint32_t c = pixels[i];
    byte  r, g, b;
    unpackColor(c, &r, &g, &b);
  
    byte ar = (byte)max(0, (((int)r + (int)rr) / 2) - 0.1);
    byte ag = (byte)max(0, (((int)g + (int)rg) / 2) - 0.1);
    byte ab = (byte)max(0, (((int)b + (int)rb) / 2) - 0.1);
 
    next_pixels[i] = strip.Color(ar, ab, ag);
    strip.setPixelColor(i, next_pixels[i]);
  }
}

void next_spread_both() {
  for (int i = 0; i < N_LEDS; i++) {

    uint32_t lc = pixels[(i-1) % N_LEDS];
    byte  lr, lg, lb;
    unpackColor(lc, &lr, &lg, &lb);

    uint32_t rc = pixels[(i+1) % N_LEDS];
    byte  rr, rg, rb;
    unpackColor(rc, &rr, &rg, &rb);

    uint32_t c = pixels[i];
    byte  r, g, b;
    unpackColor(c, &r, &g, &b);

    byte ar = (byte)(((int)lr + (int)r + (int)rr) / 3);
    byte ag = (byte)(((int)lg + (int)g + (int)rg) / 3);
    byte ab = (byte)(((int)lb + (int)b + (int)rb) / 3);

    next_pixels[i] = strip.Color(ar, ab, ag);
    strip.setPixelColor(i, next_pixels[i]);
  }
}

// Randomly set next_pixels to be a random color
void next_random_sparkle(int sparkles) {
  for (int i = 0; i < sparkles; i++) {
    int pos = random() % N_LEDS;
    
    next_pixels[pos] = colors[random() % N_COLORS];
    strip.setPixelColor(pos, next_pixels[pos]);
  }
}

void next_off() {
  for (int i = 0; i < N_LEDS; i++) {
    next_pixels[i] = strip.Color(0, 0, 0);
    strip.setPixelColor(i, strip.Color(0, 0, 0));
  }
}

// Add random noise to every next_pixel value
void next_entropy(int sparkles) {
  for (int i = 0; i < sparkles; i++) {
    uint32_t c = next_pixels[i];
    byte  r, g, b;
    unpackColor(c, &r, &g, &b);
    
    r = min(127, max(0, r + random() % 2 - 1));
    g = min(127, max(0, g + random() % 2 - 1));
    b = min(127, max(0, b + random() % 2 - 1));

    int pos = random() % N_LEDS;
    
    next_pixels[pos] = strip.Color(r, b, g);
    strip.setPixelColor(pos, next_pixels[pos]);
  }
}

void drops(int drops, float drop_create_rate) {
  // Spread all the colors around in both directions
  next_spread(0);

  if (random(1000) / 1000.0 < drop_create_rate) {
    next_random_sparkle(drops);
  }

  // entropy(10);
  
  blit();
  strip.show();
}


// Rotate pixel buffer
void blit() {
  uint32_t *tmp = pixels;
  pixels = next_pixels;
  next_pixels = tmp;
}

/* Helper functions */

void unpackColor(uint32_t c, byte* r, byte* g, byte* b) {
  *g = ((c >> 16) & 0x7f);
  *r = ((c >>  8) & 0x7f);
  *b =  (c        & 0x7f);
}


