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
#define N_COLORS     7

// Set the first variable to the NUMBER of pixels.
// The LED strips are 32 LEDs per meter but you can extend/cut the strip
LPD8806 strip = LPD8806(N_LEDS, dataPin, clockPin);

                  
uint32_t colors[] = { strip.Color(127, 0, 0), strip.Color(127, 127, 0), strip.Color(0, 127, 0), strip.Color(0, 127, 127), strip.Color(0, 0, 127), strip.Color(127, 0, 127), strip.Color(127, 127, 127) };

void setup() {
  // This is for Trinket 5V 16MHz, you can remove these three lines if you are not using a Trinket
  #if defined (__AVR_ATtiny85__)
    if (F_CPU == 16000000) clock_prescale_set(clock_div_1);
  #endif
  // End of trinket special code


  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
}


void loop() {
  stack(colors[4], 0, 1, 5);
  for (int i = 0; i < N_COLORS - 2; i++) {
    stack(colors[i], colors[(i - 1) % N_COLORS], i % 2, 0);
  }
rainbowCycleWave(0);
  
}

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

uint32_t dimColor(uint32_t c, float fraction) {
  byte  r, g, b;
  g = ((c >> 16) & 0x7f) * fraction;
  r = ((c >>  8) & 0x7f) * fraction;
  b =  (c        & 0x7f) * fraction;
  return strip.Color(r, g, b);
}
