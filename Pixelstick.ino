#include "SD.h"
#include "FastLED.h"
#include "GUIslice.h"
#include "GUIslice_ex.h"
#include "GUIslice_drv.h"

#include "arena.hpp"
#include "util.hpp"

// SD card chip select
#define SD_CS 4

// How many leds in your strip?
#define NUM_LEDS 288

#define BMP_WIDTH 341

// For led chips like Neopixels, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806 define both DATA_PIN and CLOCK_PIN
#define DATA_PIN 2

CRGB *leds = nullptr;

struct BMPFile
{
  File     file;
  boolean  flip;
  uint32_t height;
  uint32_t imageOffset;
};

BMPFile *bmpFile = nullptr;

#define GUI_MAX_FONTS           2
#define GUI_MAX_PAGES           1
#define GUI_MAX_ELEMS_RAM       1
#define GUI_MAX_ELEMS_PER_PAGE 17

gslc_tsGui     *guiGui      = nullptr;
gslc_tsDriver  *guiDriver   = nullptr;
gslc_tsPage    *guiPages    = nullptr;
gslc_tsElem    *guiElem     = nullptr;
gslc_tsElemRef *guiElemRefs = nullptr;
// Must be link-time constant as it is referenced by gslc_ElemCreateTxt_P
// and other macros, to be put into PROGMEM.
gslc_tsFont     guiFonts[GUI_MAX_FONTS];

enum {E_PG_MAIN};
enum {E_ELEM_BOX,E_ELEM_BTN_QUIT,E_ELEM_COLOR,
      E_SLIDER_R,E_SLIDER_G,E_SLIDER_B,E_ELEM_BTN_ROOM};
enum {E_FONT_TXT,E_FONT_TITLE};

#define ARENA_SIZE (1024 + sizeof(SDClass) + sizeof(BMPFile))

Arena<ARENA_SIZE> arena;

template<size_t SIZE> void *operator new(size_t size, Arena<SIZE>& a)
{
  return a.allocate(size);
}

void *operator new(size_t size, void *ptr)
{
  return ptr;
}

void operator delete(void *obj, void *alloc)
{
}

static int16_t glscDebugOut(char ch)
{
  Serial.write(ch);
  return 0;
}

bool CbBtnQuit(void* pvGui,void *pvElemRef,gslc_teTouch eTouch,int16_t nX,int16_t nY)
{
  return true;
}

void initScreen()
{
  gslc_InitDebug(&glscDebugOut);

  Serial.print(F("Initializing touchscreen..."));
  guiGui      = new (arena) gslc_tsGui();
  guiDriver   = new (arena) gslc_tsDriver();
  guiPages    = (gslc_tsPage*)arena.allocate(GUI_MAX_PAGES * sizeof(*guiPages));
  for (uint8_t i = 0; i < GUI_MAX_PAGES; ++i) {
    new ((void*)&guiPages[i]) gslc_tsPage();
  }
  guiElem     = new (arena) gslc_tsElem();
  guiElemRefs = (gslc_tsElemRef*)arena.allocate(GUI_MAX_ELEMS_PER_PAGE * sizeof(*guiElemRefs));
  for (uint8_t i = 0; i < GUI_MAX_ELEMS_PER_PAGE; ++i) {
    new ((void*)&guiElemRefs[i]) gslc_tsElemRef();
  }

  if (!gslc_Init(guiGui, guiDriver, guiPages, GUI_MAX_PAGES, guiFonts, GUI_MAX_FONTS)) {
    panic(F("failed1"));
  }

  if (!gslc_FontAdd(guiGui, E_FONT_TXT, GSLC_FONTREF_PTR, nullptr, 1)) {
    panic(F("failed2"));
  }
  if (!gslc_FontAdd(guiGui, E_FONT_TITLE, GSLC_FONTREF_PTR, nullptr, 3)) {
    panic(F("failed3"));
  }

  gslc_PageAdd(guiGui,E_PG_MAIN,guiElem,GUI_MAX_ELEMS_RAM,guiElemRefs,GUI_MAX_ELEMS_PER_PAGE);

  // Background flat color
  gslc_SetBkgndColor(guiGui,GSLC_COL_GRAY_DK2);

  // Create Title with offset shadow
  #define TMP_COL1 (gslc_tsColor){ 32, 32, 60}
  #define TMP_COL2 (gslc_tsColor){128,128,240}
  // Note: must use title Font ID
  gslc_ElemCreateTxt_P(guiGui,98,E_PG_MAIN,2,2,320,50,"Pixelstick",&guiFonts[1],
          TMP_COL1,GSLC_COL_BLACK,GSLC_COL_BLACK,GSLC_ALIGN_MID_MID,false,false);
  gslc_ElemCreateTxt_P(guiGui,99,E_PG_MAIN,0,0,320,50,"Pixelstick",&guiFonts[1],
          TMP_COL2,GSLC_COL_BLACK,GSLC_COL_BLACK,GSLC_ALIGN_MID_MID,false,false);

  // Create background box
  gslc_ElemCreateBox_P(guiGui,200,E_PG_MAIN,10,50,300,180,GSLC_COL_WHITE,GSLC_COL_BLACK,true,true,NULL,NULL);

  // Create dividers
  gslc_ElemCreateBox_P(guiGui,201,E_PG_MAIN,20,100,280,1,GSLC_COL_GRAY_DK3,GSLC_COL_BLACK,true,true,NULL,NULL);
  gslc_ElemCreateBox_P(guiGui,202,E_PG_MAIN,235,60,1,35,GSLC_COL_GRAY_DK3,GSLC_COL_BLACK,true,true,NULL,NULL);

  // Create dummy selector
  gslc_ElemCreateTxt_P(guiGui,100,E_PG_MAIN,20,65,100,20,"Selected Room:",&guiFonts[0],
          GSLC_COL_GRAY_LT2,GSLC_COL_BLACK,GSLC_COL_BLACK,GSLC_ALIGN_MID_LEFT,false,true);

  gslc_ElemCreateTxt_P(guiGui,105,E_PG_MAIN,160,115,120,20,"Set LED RGB:",&guiFonts[0],
          GSLC_COL_WHITE,GSLC_COL_BLACK,GSLC_COL_BLACK,GSLC_ALIGN_MID_LEFT,false,true);

  // Create three sliders (R,G,B) and assign callback function
  // that is invoked upon change. The common callback will update
  // the color box.

  gslc_ElemCreateTxt_P(guiGui,109,E_PG_MAIN,250,230,60,10,"GUIslice Example",&guiFonts[0],
          GSLC_COL_BLACK,GSLC_COL_BLACK,GSLC_COL_BLACK,GSLC_ALIGN_MID_RIGHT,false,false);

  gslc_SetPageCur(guiGui, E_PG_MAIN);
  Serial.println(F("successful"));
}

void deinitScreen()
{
  gslc_SetBkgndColor(guiGui,GSLC_COL_BLACK);
  gslc_Update(guiGui);

  for (uint8_t i = 0; i < GUI_MAX_ELEMS_PER_PAGE; ++i) {
    arena.destroy(&guiElemRefs[i]);
  }
  arena.destroy(guiElem);
  for (uint8_t i = 0; i < GUI_MAX_PAGES; ++i) {
    arena.destroy(&guiPages[i]);
  }
  arena.destroy(guiDriver);
  arena.destroy(guiGui);
}

void initSdCard()
{
  SdVolume::initCacheBuffer(arena.allocate(1024));
  SD = new (arena) SDClass();
  Serial.print(F("Initializing SD card..."));
  if (!SD->begin(SD_CS)) {
    panic(F("failed!"));
  }
  Serial.println(F("OK!"));
}

void deinitSdCard()
{
  arena.destroy(SD);
}

void setup(void)
{
  Serial.begin(9600);
  Serial.println(F("Pixelstick\n"));

#if 0
  initScreen();
  gslc_Update(guiGui);
  delay(10000);
  deinitScreen();
  arena.reset();
#endif

  initSdCard();
  bmpOpen("TestPad.bmp");
  for (uint16_t row = 0; row < 1; ++row) {
    bmpLoadRow(row);
    #if 0
    FastLED.addLeds<WS2812B, DATA_PIN, RGB>(leds, NUM_LEDS);
    FastLED.setBrightness(1);
    FastLED.show();
    #endif
  }
  bmpFile->file.close();
  deinitSdCard();
  arena.reset();

  //initScreen();
}

void loop()
{
  gslc_Update(guiGui);
}

void bmpOpen(const char *filename)
{
  Serial.println();
  Serial.print(F("Loading image "));
  Serial.println(filename);

  bmpFile = new (arena) BMPFile;
  bmpFile->file = SD->open(filename);
  if (!bmpFile->file) {
    panic(F("File not found"));
  }

  // Parse BMP header
  if (bmpRead16() != 0x4D42) {
    panic(F("Invalid header"));
  }
  Serial.print(F("File size: "));
  Serial.println(bmpRead32());
  (void)bmpRead32(); // Ignore reserved word
  bmpFile->imageOffset = bmpRead32();
  Serial.print(F("Image Offset: "));
  Serial.println(bmpFile->imageOffset, DEC);

  // Read BMP Info header
  Serial.print(F("Header size: "));
  Serial.println(bmpRead32());

  if (bmpRead32() != BMP_WIDTH) {
    panic(F("Width must be 341"));
  }

  uint32_t height = bmpRead32();
  boolean  flip   = true;
  if (height < 0) {
    height = -height;
    flip   = false;
  }
  bmpFile->height = height;
  bmpFile->flip   = flip;

  if (bmpRead16() != 1) {
    panic(F("Planes must be 1"));
  }
  if (bmpRead16() != 16) {
    panic(F("Depth must be 16"));
  }
  const uint32_t depth = bmpRead32();
  if (depth != 0 && depth != 3) {
    panic(F("Must be uncompressed"));
  }

  // BMP rows are padded (if needed) to 4-byte boundary
  const uint32_t rowSize = (BMP_WIDTH * 3U + 3U) & ~3U;

  // If bmpHeight is negative, image is in top-down order.
  // This is not canon but has been observed in the wild.
  Serial.print(F("Image size: 341"));
  Serial.print('x');
  Serial.println(bmpFile->height);
  Serial.print(F("Row size: "));
  Serial.println(rowSize);
}

void bmpLoadRow(uint32_t row)
{
  const uint32_t rowSize = (BMP_WIDTH * 3U + 3U) & ~3U;

  uint32_t pos;
  if (bmpFile->flip) { // Normal case
    pos = bmpFile->imageOffset + (bmpFile->height - 1 - row) * rowSize;
  } else {
    pos = bmpFile->imageOffset + row * rowSize;
  }

  File& file = bmpFile->file;
  if (file.position() != pos) {
    file.seek(pos);
  }

  uint8_t *rawData = (uint8_t*)SdVolume::getRawCacheBuffer();
  uint8_t *ledMem  = rawData + 1024 - NUM_LEDS * sizeof(CRGB);
  for (int16_t i = NUM_LEDS - 1; i >= 0; --i) {
    uint16_t c;
    c  = rawData[2 * i + 0] << 8U;
    c |= rawData[2 * i + 1] << 0U;

    const uint8_t r = (c & 0x7C00U) >> 10;
    const uint8_t g = (c & 0x03E0U) >>  5;
    const uint8_t b = (c & 0x001FU) >>  0;

    new ((void*)&ledMem[i]) CRGB(r, g, b);
  }
  leds = (CRGB*)ledMem;
}

// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.
uint16_t bmpRead16()
{
  File& f = bmpFile->file;
  uint16_t result;
  ((uint8_t*)&result)[0] = f.read(); // LSB
  ((uint8_t*)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t bmpRead32()
{
  File& f = bmpFile->file;
  uint32_t result;
  ((uint8_t*)&result)[0] = f.read(); // LSB
  ((uint8_t*)&result)[1] = f.read();
  ((uint8_t*)&result)[2] = f.read();
  ((uint8_t*)&result)[3] = f.read(); // MSB
  return result;
}

// vim: et ts=2
