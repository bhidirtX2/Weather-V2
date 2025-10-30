/*
  +------------------------------+
  |                              |
  |       FOR ESP-01 ONLY        |
  |                              |
  +------------------------------+
*/

#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <coredecls.h>                  // settimeofday_cb()
#else
#include <WiFi.h>
#endif
#include <ESPHTTPClient.h>
#include <JsonListener.h>

// time
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval

#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"
#include "Wire.h"
#include "OpenWeatherMapCurrent.h"
#include "OpenWeatherMapForecast.h"
#include "WeatherStationFonts.h"
#include "WeatherStationImages.h"

const char* WIFI_SSID = "Dimas";
const char* WIFI_PWD = "dhimas12345";

#define TZ              6     
#define DST_MN          60     

const int UPDATE_INTERVAL_SECS = 20 * 60;

const int I2C_DISPLAY_ADDRESS = 0x3c;
#if defined(ESP8266)
const int SDA_PIN = 0;
const int SDC_PIN = 2;
#else
const int SDA_PIN = 5; //D3;
const int SDC_PIN = 4; //D4;
#endif

const unsigned char humidityIcon[] PROGMEM = {
  B00011000,
  B00111100,
  B01111110,
  B01111110,
  B00111100,
  B00011000,
  B00011000,
  B00000000
};

const unsigned char pressureIcon[] PROGMEM = {
B00000000,
  B11110000,
  B00000000,
  B01111100,
  B00000000,
  B00011111,
  B00000000,
  B00000000
};

const unsigned char sunriseIcon[] PROGMEM = {
  B00111000,
  B01000100,
  B10000010,
  B10000010,
  B10000010,
  B01000100,
  B00111000,
  B00000000
};

const unsigned char sunsetIcon[] PROGMEM = {
 B00000000,
  B00000000,
  B00111000,
  B01000100,
  B10000010,
  B11111110,
  B00011000,
  B00000000
};

// https://docs.thingpulse.com/how-tos/openweathermap-key/
String OPEN_WEATHER_MAP_APP_ID = "58e17d0a4b50ef91d892c6e555cab8c1";

//Use the OWM GeoCoder API to find lat/lon for your city: https://openweathermap.org/api/geocoding-api
float OPEN_WEATHER_MAP_LOCATION_LAT = -8.166;
float OPEN_WEATHER_MAP_LOCATION_LON = 113.7032;

// Pick a language code from this list:
// Arabic - ar, Bulgarian - bg, Catalan - ca, Czech - cz, German - de, Greek - el,
// English - en, Persian (Farsi) - fa, Finnish - fi, French - fr, Galician - gl,
// Croatian - hr, Hungarian - hu, Italian - it, Japanese - ja, Korean - kr,
// Latvian - la, Lithuanian - lt, Macedonian - mk, Dutch - nl, Polish - pl,
// Portuguese - pt, Romanian - ro, Russian - ru, Swedish - se, Slovak - sk,
// Slovenian - sl, Spanish - es, Turkish - tr, Ukrainian - ua, Vietnamese - vi,
// Chinese Simplified - zh_cn, Chinese Traditional - zh_tw.

String OPEN_WEATHER_MAP_LANGUAGE = "en";
const uint8_t MAX_FORECASTS = 5;

const boolean IS_METRIC = true;

const String WDAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const String MONTH_NAMES[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

SSD1306Wire     display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
OLEDDisplayUi   ui( &display );

OpenWeatherMapCurrentData currentWeather;
OpenWeatherMapCurrent currentWeatherClient;

OpenWeatherMapForecastData forecasts[MAX_FORECASTS];
OpenWeatherMapForecast forecastClient;

#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)
time_t now;

bool readyForWeatherUpdate = false;

String lastUpdate = "--";

long timeSinceLastWUpdate = 0;

void drawProgress(OLEDDisplay *display, int percentage, String label);
void updateData(OLEDDisplay *display);
void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);
void drawWeatherDetails(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state);
void setReadyForWeatherUpdate();

FrameCallback frames[] = { drawDateTime, drawCurrentWeather, drawForecast, drawWeatherDetails };
int numberOfFrames = 4;

OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println();

  // initialize dispaly
  display.init();
  display.clear();
  display.display();

  //display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);

  WiFi.begin(WIFI_SSID, WIFI_PWD);

  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    display.clear();
    display.drawString(64, 10, "Connecting to WiFi");
    display.drawXbm(46, 30, 8, 8, counter % 3 == 0 ? activeSymbole : inactiveSymbole);
    display.drawXbm(60, 30, 8, 8, counter % 3 == 1 ? activeSymbole : inactiveSymbole);
    display.drawXbm(74, 30, 8, 8, counter % 3 == 2 ? activeSymbole : inactiveSymbole);
    display.display();

    counter++;
  }
  // Get time from network time service
  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");

  ui.setTargetFPS(30);

  ui.setActiveSymbol(activeSymbole);
  ui.setInactiveSymbol(inactiveSymbole);

  ui.setIndicatorPosition(BOTTOM);

  ui.setIndicatorDirection(LEFT_RIGHT);

  ui.setFrameAnimation(SLIDE_LEFT);

  ui.setFrames(frames, numberOfFrames);

  ui.setOverlays(overlays, numberOfOverlays);

  ui.init();

  Serial.println("");

  updateData(&display);

}

void loop() {

  if (millis() - timeSinceLastWUpdate > (1000L*UPDATE_INTERVAL_SECS)) {
    setReadyForWeatherUpdate();
    timeSinceLastWUpdate = millis();
  }

  if (readyForWeatherUpdate && ui.getUiState()->frameState == FIXED) {
    updateData(&display);
  }

  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0) {

    delay(remainingTimeBudget);
  }
}

void drawProgress(OLEDDisplay *display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 10, percentage);
  display->display();
}

void updateData(OLEDDisplay *display) {
  drawProgress(display, 10, "Updating time...");
  drawProgress(display, 30, "Updating weather...");
  currentWeatherClient.setMetric(IS_METRIC);
  currentWeatherClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  currentWeatherClient.updateCurrent(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_LAT, OPEN_WEATHER_MAP_LOCATION_LON);
  drawProgress(display, 50, "Updating forecasts...");
  forecastClient.setMetric(IS_METRIC);
  forecastClient.setLanguage(OPEN_WEATHER_MAP_LANGUAGE);
  uint8_t allowedHours[] = {12};
  forecastClient.setAllowedHours(allowedHours, sizeof(allowedHours));
  forecastClient.updateForecasts(forecasts, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_LAT, OPEN_WEATHER_MAP_LOCATION_LON, MAX_FORECASTS);

  readyForWeatherUpdate = false;
  drawProgress(display, 100, "Done...");
  delay(1000);
}

void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[16];


  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  String date = WDAY_NAMES[timeInfo->tm_wday];

  sprintf_P(buff, PSTR("%s, %02d/%02d/%04d"), WDAY_NAMES[timeInfo->tm_wday].c_str(), timeInfo->tm_mday, timeInfo->tm_mon+1, timeInfo->tm_year + 1900);
  display->drawString(64 + x, 5 + y, String(buff));
  display->setFont(ArialMT_Plain_24);

  sprintf_P(buff, PSTR("%02d:%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  display->drawString(64 + x, 15 + y, String(buff));
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->drawString(64 + x, 38 + y, currentWeather.description);

  display->setFont(ArialMT_Plain_24);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  String temp = String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F");
  display->drawString(60 + x, 5 + y, temp);

 display->setTextAlignment(TEXT_ALIGN_CENTER);

  String icon = currentWeather.icon; 
  if (icon.startsWith("01")) {
    drawAnimatedSun(display, x, y);
  } else if (icon.startsWith("02") || icon.startsWith("03") || icon.startsWith("04")) {
    drawAnimatedCloud(display, x, y);
  } else if (icon.startsWith("09") || icon.startsWith("10")) {
    drawAnimatedRain(display, x, y);
  } else {
    display->setFont(Meteocons_Plain_36);
    display->drawString(32 + x, 0 + y, currentWeather.iconMeteoCon);
  }
}

void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  drawForecastDetails(display, x, y, 0);
  drawForecastDetails(display, x + 44, y, 1);
  drawForecastDetails(display, x + 88, y, 2);
}

void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex) {
  time_t observationTimestamp = forecasts[dayIndex].observationTime;
  struct tm* timeInfo;
  timeInfo = localtime(&observationTimestamp);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y, WDAY_NAMES[timeInfo->tm_wday]);

  display->setFont(Meteocons_Plain_21);
  display->drawString(x + 20, y + 12, forecasts[dayIndex].iconMeteoCon);
  String temp = String(forecasts[dayIndex].temp, 0) + (IS_METRIC ? "°C" : "°F");
  display->setFont(ArialMT_Plain_10);
  display->drawString(x + 20, y + 34, temp);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawWeatherDetails(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(ArialMT_Plain_10);

  // kelembapan
  display->drawXbm(0 + x, 2 + y, 8, 8, humidityIcon);
  display->drawString(12 + x, 0 + y, " Humidity: " + String(currentWeather.humidity) + "%");

  // tekanan udara
  display->drawXbm(0 + x, 14 + y, 8, 8, pressureIcon);
  display->drawString(12 + x, 12 + y, " Pressure: " + String(currentWeather.pressure) + " hPa");

  // sunrise
  time_t tSunrise = currentWeather.sunrise;
  struct tm* sunriseTm = localtime(&tSunrise);
  char buf1[6];
  sprintf(buf1, "%02d:%02d", sunriseTm->tm_hour, sunriseTm->tm_min);
  display->drawXbm(0 + x, 26 + y, 8, 8, sunriseIcon);
  display->drawString(12 + x, 24 + y, " Sunrise: " + String(buf1));

  // sunset
  time_t tSunset = currentWeather.sunset;
  struct tm* sunsetTm = localtime(&tSunset);
  char buf2[6];
  sprintf(buf2, "%02d:%02d", sunsetTm->tm_hour, sunsetTm->tm_min);
  display->drawXbm(0 + x, 38 + y, 8, 8, sunsetIcon);
  display->drawString(12 + x, 36 + y, " Sunset: " + String(buf2));
}

void drawAnimatedSun(OLEDDisplay *display, int16_t x, int16_t y) {
  static int angle = 0;
  int cx = x + 32;
  int cy = y + 20;

  display->fillCircle(cx, cy, 10);

  for (int i = 0; i < 8; i++) {
    float a = (angle + i * 45) * 3.14 / 180;
    int x1 = cx + cos(a) * 14;
    int y1 = cy + sin(a) * 14;
    int x2 = cx + cos(a) * 20;
    int y2 = cy + sin(a) * 20;
    display->drawLine(x1, y1, x2, y2);
  }
  angle = (angle + 10) % 360;
}

void drawAnimatedCloud(OLEDDisplay *display, int16_t x, int16_t y) {
  static int offset = 0;
  int cx = x + (offset % 30) - 10;
  int cy = y + 20;

  display->fillCircle(cx, cy, 10);
  display->fillCircle(cx + 12, cy + 5, 12);
  display->fillCircle(cx + 24, cy, 10);

  offset++;
}

void drawAnimatedRain(OLEDDisplay *display, int16_t x, int16_t y) {
  static int dropY = 0;

  display->fillCircle(x + 20, y + 10, 10);
  display->fillCircle(x + 32, y + 12, 12);
  display->fillCircle(x + 44, y + 10, 10);

  for (int i = 0; i < 5; i++) {
    int dx = 20 + i * 10;
    int dy = (dropY + i * 6) % 30;
    display->drawLine(x + dx, y + 25 + dy, x + dx, y + 28 + dy);
  }
  dropY = (dropY + 2) % 30;
}

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  now = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&now);
  char buff[14];
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);

  display->setColor(WHITE);
  display->setFont(ArialMT_Plain_10);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 54, String(buff));
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->drawString(128, 54, currentWeather.cityName);

  display->drawHorizontalLine(0, 52, 128);
}


void setReadyForWeatherUpdate() {
  Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}
