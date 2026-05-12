#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <AnimatedGIF.h>

#include <esp_heap_caps.h>

#include "keyboard_cat_gif.h"

#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST  8
#define TFT_MISO 13
#define TFT_BL   21

static constexpr int16_t DISPLAY_WIDTH = 320;
static constexpr int16_t DISPLAY_HEIGHT = 240;
static constexpr uint32_t TARGET_FRAME_US = 1000000 / 30;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
AnimatedGIF gif;
static uint16_t lineBuffer[DISPLAY_WIDTH];
static uint8_t *frameBuffer = nullptr;
static size_t frameBufferSize = 0;
static bool useCookedBuffer = false;

void waitForNextFrame(uint32_t frameStart)
{
	uint32_t frameTime = micros() - frameStart;
	if (frameTime >= TARGET_FRAME_US)
		return;

	uint32_t waitTime = TARGET_FRAME_US - frameTime;
	if (waitTime > 1000)
		delay(waitTime / 1000);
	delayMicroseconds(waitTime % 1000);
}

void GIFDraw(GIFDRAW *pDraw)
{
	// Minimal GIFDraw: converted palette -> 16-bit pixels and write
	int y = pDraw->iY + pDraw->y;
	int iWidth = pDraw->iWidth;

	if (iWidth + pDraw->iX > DISPLAY_WIDTH)
		iWidth = DISPLAY_WIDTH - pDraw->iX;
	if (y >= DISPLAY_HEIGHT || pDraw->iX >= DISPLAY_WIDTH || iWidth < 1)
		return;

	if (pDraw->ucHasTransparency) {
		// handle transparency runs
		uint8_t *s = pDraw->pPixels;
		uint16_t *usPalette = pDraw->pPalette;
		uint8_t *pEnd = s + iWidth;
		int x = 0;
		while (x < iWidth) {
			int iCount = 0;
			while (s < pEnd && *s != pDraw->ucTransparent) {
				lineBuffer[iCount++] = usPalette[*s++];
			}
			if (iCount) {
				tft.setAddrWindow(pDraw->iX + x, y, iCount, 1);
				tft.writePixels(lineBuffer, iCount, true, true);
				x += iCount;
			}
			// skip transparent pixels
			while (s < pEnd && *s == pDraw->ucTransparent) {
				s++; x++;
			}
		}
	} else {
		uint8_t *s = pDraw->pPixels;
		uint16_t *usPalette = pDraw->pPalette;
		for (int x = 0; x < iWidth; x++)
			lineBuffer[x] = usPalette[s[x]];
		tft.setAddrWindow(pDraw->iX, y, iWidth, 1);
		tft.writePixels(lineBuffer, iWidth, true, true);
	}
}

void playEmbeddedGif()
{
	if (!gif.openFLASH((uint8_t *)keyboard_cat_gif, sizeof(keyboard_cat_gif), GIFDraw)) {
		Serial.print("GIF open failed: ");
		Serial.println(gif.getLastError());
		delay(1000);
		return;
	}

	useCookedBuffer = false;
	size_t cookedBufferSize = (size_t)gif.getCanvasWidth() * (size_t)gif.getCanvasHeight() * 3;
	if (frameBuffer == nullptr || frameBufferSize < cookedBufferSize) {
		if (frameBuffer != nullptr)
			heap_caps_free(frameBuffer);
		frameBuffer = (uint8_t *)heap_caps_malloc(cookedBufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		frameBufferSize = frameBuffer != nullptr ? cookedBufferSize : 0;
	}

	if (frameBuffer != nullptr) {
		gif.setFrameBuf(frameBuffer);
		gif.setDrawType(GIF_DRAW_COOKED);
		useCookedBuffer = true;
	} else {
		gif.setFrameBuf(nullptr);
		gif.setDrawType(GIF_DRAW_RAW);
	}

	tft.startWrite();
	while (true) {
		uint32_t frameStart = micros();
		if (!gif.playFrame(false, NULL))
			break;
		waitForNextFrame(frameStart);
		yield();
	}
	tft.endWrite();
	gif.close();
}

void setup()
{
	Serial.begin(115200);
	delay(500);
	Serial.println("\n=== ESP32 GIF Player ===");

	// Backlight on pin 21
	pinMode(TFT_BL, OUTPUT);
	digitalWrite(TFT_BL, HIGH);
	Serial.println("BL pin HIGH");

	// Manual reset pulse on pin 8
	pinMode(TFT_RST, OUTPUT);
	digitalWrite(TFT_RST, LOW);
	delay(100);
	digitalWrite(TFT_RST, HIGH);
	delay(100);
	Serial.println("RST pulse done");

	// Configure CS high, DC as output
	pinMode(TFT_CS, OUTPUT);
	digitalWrite(TFT_CS, HIGH);
	pinMode(TFT_DC, OUTPUT);
	Serial.println("CS/DC pins configured");

	SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
	Serial.println("SPI.begin() done");

	// Initialize TFT at a faster SPI speed to reduce visible redraw flicker
	tft.begin(40000000);
	Serial.println("tft.begin(40MHz) done");

	tft.setRotation(3);
	tft.fillScreen(ILI9341_BLACK);
	Serial.println("fillScreen(BLACK) done");

	// Initialize GIF decoder with big-endian pixel order (matches RGB565)
	gif.begin(BIG_ENDIAN_PIXELS);
	Serial.println("Setup complete - GIF playback starting");
}

void loop()
{
	playEmbeddedGif();
}
