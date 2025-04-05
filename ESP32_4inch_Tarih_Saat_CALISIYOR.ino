#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <SPIFFS.h> // Use SPIFFS
#include <Arduino_GFX_Library.h>
#include "ESP32_S3_4inch_Touch_Screen.h" // Your original hardware config
#include "TAMC_GT911.h" // Include touch header explicitly

// --- Wi-Fi & NTP ---
const char* ssid = "_________"; // <<< YOUR WIFI SSID
const char* password = "Otsoc1954"; // <<< YOUR WIFI PASSWORD
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3 * 3600;
const int   daylightOffset_sec = 0;

// --- OpenWeatherMap ---
const char* owmApiKey = "98a9d56290aad5c05211a186f657db3b"; // <<< YOUR OWM API KEY
const char* owmCity = "Istanbul";
const char* owmLanguage = "tr";
const char* owmUnits = "metric";

// --- Update Intervals ---
const unsigned long time_update_interval = 1000;
const unsigned long weather_update_interval = 15 * 60 * 1000;

// --- Color Defines (Arduino_GFX colors) ---
#define BG_COLOR     BLACK
#define TIME_COLOR   WHITE
#define DATE_COLOR   LIGHTGREY
#define TEMP_COLOR   WHITE
#define MINMAX_COLOR CYAN
#define DESC_COLOR   LIGHTGREY
#define HUMID_COLOR  BLUE
#define ICON_RECT_COLOR WHITE // Fallback icon rectangle
#define TOUCH_COLOR  MAGENTA
#define COLON_COLOR  TIME_COLOR

// --- Global Variables ---
unsigned long last_time_update = 0;
unsigned long last_weather_update = 0;
char time_hh_buffer[3];
char time_mm_buffer[3];
char seconds_buffer[3];
char date_buffer[12];
char day_buffer[15];
int previous_minute = -1;
int previous_day = -1;
int16_t seconds_x = 0;
int16_t seconds_y = 0;
uint16_t seconds_w = 0;
uint16_t seconds_h = 0;

// *** ADDED: Define clock top-left position ***
const int16_t CLOCK_START_X = 10; // Shifted left from center
const int16_t CLOCK_START_Y = 10; // Shifted up from 20

bool time_synced = false;
float current_temp = -99.9;
float temp_min = -99.9;
float temp_max = -99.9;
int current_humidity = -1;
String weather_description = "---";
String weather_icon_code = "unknown";
bool weather_valid = false;
#define BUFFPIXEL 20 // For BMP drawing

// --- Function Declarations ---
void connectWiFi();
void syncTime();
bool getWeatherData();
void updateDateDayDisplay(struct tm &timeinfo);
void updateHourMinuteDisplay(struct tm &timeinfo);
void updateSecondsDisplay(struct tm &timeinfo);
void updateWeatherDisplay();
// *** MODIFIED: drawSeparator draws two dots ***
void drawSeparator(int16_t x, int16_t y, uint16_t text_h);
uint16_t read16(fs::File &f);
uint32_t read32(fs::File &f);
bool drawBmpFromFile(fs::FS &fs, const char *filename, int16_t x, int16_t y);

// ======================================================================
// SETUP (Remains the same logic, uses new Y coords implicitly)
// ======================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32-S3 Weather Station - Colon Fix & Positioning");

  tp.begin();
  tp.setRotation(ROTATION_INVERTED);
  gfx->begin();
  gfx->fillScreen(BG_COLOR);
  Serial.println("Display & Touch Initialized.");

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  Serial.println("Backlight ON.");
#endif

  Serial.print("Mounting SPIFFS... ");
  if(!SPIFFS.begin(true)){
      Serial.println("SPIFFS Mount Failed!");
      gfx->setTextSize(2); gfx->setTextColor(RED, BG_COLOR);
      gfx->setCursor(10, 100); gfx->println("Dosya Sistemi Hatasi!");
  } else {
      Serial.println("SPIFFS Mounted.");
  }

  gfx->setTextSize(2);
  gfx->setTextColor(DATE_COLOR, BG_COLOR);
  gfx->setCursor(10, 10);
  gfx->println("Baslatiliyor...");

  connectWiFi();

  struct tm timeinfo;
  if (WiFi.status() == WL_CONNECTED) {
    syncTime();
    getWeatherData();
  } else {
    gfx->setCursor(10, 80);
    gfx->setTextColor(RED, BG_COLOR);
    gfx->println("WiFi Baglantisi Yok!");
    getWeatherData();
  }

  gfx->fillScreen(BG_COLOR);

  if (time_synced && getLocalTime(&timeinfo)) {
    updateDateDayDisplay(timeinfo);    // Uses new CLOCK_START_Y reference
    updateHourMinuteDisplay(timeinfo); // Uses new CLOCK_START_X/Y
    updateSecondsDisplay(timeinfo);    // Uses coords from HourMinute
    previous_minute = timeinfo.tm_min;
    previous_day = timeinfo.tm_mday;
  } else {
      gfx->setTextSize(9); gfx->setTextColor(TIME_COLOR, BG_COLOR);
      gfx->setCursor(CLOCK_START_X, CLOCK_START_Y); gfx->print("-- -- --");
      gfx->setTextSize(3); gfx->setTextColor(DATE_COLOR, BG_COLOR);
      int date_day_y = CLOCK_START_Y + 80; // Adjust based on new clock Y
      gfx->setCursor(20, date_day_y); gfx->print("--/--/----");
      gfx->setCursor(gfx->width()-80, date_day_y); gfx->print("---");
  }
  updateWeatherDisplay(); // Uses new CLOCK_START_Y reference

  Serial.println("Setup complete.");
  last_time_update = millis();
  last_weather_update = millis();
}

// ======================================================================
// LOOP (Remains the same)
// ======================================================================
void loop() {
  tp.read();
  if (tp.isTouched && tp.points[0].x < gfx->width() && tp.points[0].y < gfx->height()) {
    gfx->fillCircle(tp.points[0].x, tp.points[0].y, 5, TOUCH_COLOR);
  }

  unsigned long current_millis = millis();
  struct tm timeinfo;

  if (time_synced && (current_millis - last_time_update >= time_update_interval)) {
    if (getLocalTime(&timeinfo)) {

      if (timeinfo.tm_mday != previous_day || previous_day == -1) {
        updateDateDayDisplay(timeinfo);
        previous_day = timeinfo.tm_mday;
        previous_minute = -1;
      }

      if (timeinfo.tm_min != previous_minute || previous_minute == -1) {
        updateHourMinuteDisplay(timeinfo);
        previous_minute = timeinfo.tm_min;
      }

      updateSecondsDisplay(timeinfo);
      last_time_update = current_millis;
    }
  }

  if (WiFi.status() == WL_CONNECTED && (current_millis - last_weather_update >= weather_update_interval)) {
    if (getWeatherData()) {
      updateWeatherDisplay();
    }
    last_weather_update = current_millis;
  }

  if (current_millis < last_time_update) { last_time_update = current_millis; }
  if (current_millis < last_weather_update) { last_weather_update = current_millis; }
}

// connectWiFi(), syncTime(), getWeatherData() - Remain the same
void connectWiFi() {
    Serial.print("Connecting to WiFi: "); Serial.println(ssid);
    gfx->setTextSize(2); gfx->setTextColor(YELLOW, BG_COLOR);
    gfx->setCursor(10, 50); gfx->print("WiFi Baglaniyor...");
    WiFi.begin(ssid, password);
    unsigned long connect_start_time = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - connect_start_time < 15000)) {
      delay(250); Serial.print("."); gfx->print(".");
      if (gfx->getCursorX() > gfx->width() - 20) { gfx->setCursor(gfx->getCursorX() - 10, gfx->getCursorY()); gfx->print(" "); }
    }
    gfx->fillRect(10, 50, gfx->width() - 20, 20, BG_COLOR);
    gfx->setCursor(10, 50);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!"); Serial.print("IP Address: "); Serial.println(WiFi.localIP());
      gfx->setTextColor(GREEN, BG_COLOR); gfx->print("WiFi Bagli: "); gfx->println(WiFi.localIP());
    } else {
      Serial.println("\nWiFi connection FAILED!");
      gfx->setTextColor(RED, BG_COLOR); gfx->println("WiFi Baglantisi Basarisiz!");
    }
    delay(1000);
}

void syncTime() {
    Serial.println("NTP Zaman Senkronizasyonu...");
    gfx->setTextSize(2); gfx->setTextColor(WHITE, BG_COLOR);
    gfx->setCursor(10, 80); gfx->print("Zaman Ayarlaniyor...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10000)) {
      Serial.println("NTP Hatasi.");
      gfx->fillRect(10, 80, gfx->width() - 20, 20, BG_COLOR);
      gfx->setCursor(10, 80); gfx->setTextColor(RED, BG_COLOR); gfx->println("NTP Zaman Hatasi!");
      time_synced = false; return;
    }
    Serial.println("Zaman Ayarlandi.");
    gfx->fillRect(10, 80, gfx->width() - 20, 20, BG_COLOR);
    gfx->setCursor(10, 80); gfx->setTextColor(GREEN, BG_COLOR); gfx->println("Zaman Ayarlandi!");
    time_synced = true;
    delay(1000);
}

bool getWeatherData() {
    weather_valid = false;
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("getWeatherData: WiFi not connected. Can't update.");
        return false;
    }
    HTTPClient http;
    String apiUrl = "http://api.openweathermap.org/data/2.5/weather?q=";
    apiUrl += owmCity; apiUrl += "&appid="; apiUrl += owmApiKey;
    apiUrl += "&units="; apiUrl += owmUnits; apiUrl += "&lang="; apiUrl += owmLanguage;
    Serial.print("Hava Durumu Aliniyor: "); Serial.println(apiUrl);
    http.begin(apiUrl); int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        #if ARDUINOJSON_VERSION_MAJOR == 6
        DynamicJsonDocument doc(1536); DeserializationError error = deserializeJson(doc, payload);
        #else
        JsonDocument doc; DeserializationError error = deserializeJson(doc, payload);
        #endif
        if (!error) {
            bool temp_ok=false,min_ok=false,max_ok=false,humid_ok=false,desc_ok=false,icon_ok=false;
            float parsed_temp = -99.9; float parsed_min = -99.9; float parsed_max = -99.9;
            int parsed_humidity = -1; String parsed_desc = "---"; String parsed_icon = "unknown";

            if (doc.containsKey("main")) {
                if(doc["main"].containsKey("temp")) { parsed_temp = doc["main"]["temp"]; temp_ok = true; }
                if(doc["main"].containsKey("temp_min")) { parsed_min = doc["main"]["temp_min"]; min_ok = true; }
                if(doc["main"].containsKey("temp_max")) { parsed_max = doc["main"]["temp_max"]; max_ok = true; }
                if(doc["main"].containsKey("humidity")) { parsed_humidity = doc["main"]["humidity"]; humid_ok = true; } }
            if (doc.containsKey("weather") && doc["weather"][0]) {
                if(doc["weather"][0].containsKey("description")) {
                    parsed_desc = doc["weather"][0]["description"].as<String>();
                    if (parsed_desc.length() > 0) { parsed_desc.setCharAt(0, toupper(parsed_desc.charAt(0))); }
                    desc_ok = true; }
                if(doc["weather"][0].containsKey("icon")) { parsed_icon = doc["weather"][0]["icon"].as<String>(); icon_ok = true; } }

            if (temp_ok && min_ok && max_ok && humid_ok && desc_ok && icon_ok) {
                 Serial.printf("Yeni Hava Durumu: %.1fC (%.1f/%.1f), %d%%, %s, Icon: %s\n", parsed_temp, parsed_min, parsed_max, parsed_humidity, parsed_desc.c_str(), parsed_icon.c_str());
                 current_temp = parsed_temp;
                 temp_min = parsed_min;
                 temp_max = parsed_max;
                 current_humidity = parsed_humidity;
                 weather_description = parsed_desc;
                 weather_icon_code = parsed_icon;
                 weather_valid = true;
            } else { Serial.println("JSON ayrıştırma eksik."); weather_valid = false;}
        } else { Serial.print("JSON Hatasi: "); Serial.println(error.c_str()); weather_valid = false;}
    } else { Serial.printf("HTTP GET Hatasi: %d\n", httpCode); weather_valid = false; }
    http.end();
    if (!weather_valid) {
         Serial.println("Hava durumu verisi alınamadı veya işlenemedi. Eski veri kullanılıyor (varsa).");
    }
    return weather_valid;
}

// ======================================================================
// MODIFIED: Uses new CLOCK_START_Y reference for positioning
// ======================================================================
void updateDateDayDisplay(struct tm &timeinfo) {
  Serial.printf("Updating date/day display (Day: %d)\n", timeinfo.tm_mday);

  strftime(date_buffer, sizeof(date_buffer), "%d/%m/%Y", &timeinfo);
  const char* days_tr[] = {"Pazar", "Pazartesi", "Sali", "Carsamba", "Persembe", "Cuma", "Cumartesi"};
  int day_index = timeinfo.tm_wday;
  if (day_index < 0 || day_index > 6) day_index = 0;
  strcpy(day_buffer, days_tr[day_index]);

  // Calculate Y position based on the (new) clock Y position
  int date_day_y = CLOCK_START_Y + 80; // Position below the clock

  gfx->fillRect(0, date_day_y, gfx->width(), 30, BG_COLOR);
  gfx->setTextSize(3); gfx->setTextColor(DATE_COLOR, BG_COLOR);
  gfx->setCursor(20, date_day_y); gfx->print(date_buffer);
  int day_w_pixels = strlen(day_buffer) * 3 * 6;
  gfx->setCursor(gfx->width() - day_w_pixels - 20, date_day_y);
  gfx->print(day_buffer);
}


// ======================================================================
// MODIFIED: Draws HH [sep] MM [sep] using fixed start coordinates and increased spacing
// ======================================================================
void updateHourMinuteDisplay(struct tm &timeinfo) {
  Serial.printf("Updating HH:MM display (Minute: %d)\n", timeinfo.tm_min);

  strftime(time_hh_buffer, sizeof(time_hh_buffer), "%H", &timeinfo);
  strftime(time_mm_buffer, sizeof(time_mm_buffer), "%M", &timeinfo);

  // Use defined start Y coordinate
  int time_y = CLOCK_START_Y;

  gfx->setTextSize(9);
  gfx->setTextColor(TIME_COLOR, BG_COLOR);

  // --- Define separator geometry and spacing ---
  // Separator dimensions defined in drawSeparator now
  const int16_t spacing = 8; // Increased spacing

  // --- Calculate widths ---
  uint16_t hh_w, hh_h, mm_w, mm_h, ss_w, ss_h;
  int16_t dummy_x1, dummy_y1;
  char dummy_num[] = "00";

  // Get text height and typical width from "00"
  gfx->getTextBounds(dummy_num, 0, 0, &dummy_x1, &dummy_y1, &ss_w, &ss_h);
  hh_w = ss_w; // Approximation for HH width
  mm_w = ss_w; // Approximation for MM width
  hh_h = ss_h; // Use calculated text height

  // --- Use fixed start X coordinate ---
  int16_t current_x = CLOCK_START_X;

  // --- Clear area needed for HH [sep] MM [sep] ---
  // Need width of HH, sep1, MM, sep2 (separator width from drawSeparator)
  const uint16_t separator_width = 6; // Width defined in drawSeparator
  uint16_t clear_w = hh_w + spacing + separator_width + spacing + mm_w + spacing + separator_width;
  gfx->fillRect(current_x, time_y, clear_w + spacing, hh_h + 5, BG_COLOR); // Clear with padding

  // --- Draw HH ---
  gfx->setCursor(current_x, time_y);
  gfx->print(time_hh_buffer);
  current_x += hh_w;

  // --- Draw First Separator ---
  current_x += spacing;
  drawSeparator(current_x, time_y, hh_h); // Helper function draws two dots
  current_x += separator_width; // Advance by separator width

  // --- Draw MM ---
  current_x += spacing;
  gfx->setCursor(current_x, time_y);
  gfx->print(time_mm_buffer);
  current_x += mm_w;

  // --- Draw Second Separator ---
  current_x += spacing;
  drawSeparator(current_x, time_y, hh_h); // Helper function draws two dots
  current_x += separator_width; // Advance by separator width

  // --- Store Seconds Coordinates ---
  seconds_x = current_x + spacing; // Start SS after last separator + spacing
  seconds_y = time_y;
  seconds_w = ss_w; // Width calculated earlier
  seconds_h = hh_h; // Height calculated earlier
}

// ======================================================================
// MODIFIED: Helper function draws two small squares like a colon
// ======================================================================
void drawSeparator(int16_t x, int16_t y, uint16_t text_height) {
    // Dimensions for the two dots
    const uint16_t separator_width = 6; // Width of each "dot"
    const uint16_t dot_height = 6;      // Height of each "dot"
    const uint16_t dot_gap = 6;         // Vertical gap between dots

    // Calculate total height of the two-dot structure
    uint16_t total_sep_height = dot_height + dot_gap + dot_height;

    // Calculate top Y position to center the structure vertically within text height
    int16_t top_dot_y = y + (text_height - total_sep_height) / 2;
    if (top_dot_y < y) top_dot_y = y; // Ensure it doesn't go above the text line

    // Calculate bottom Y position
    int16_t bottom_dot_y = top_dot_y + dot_height + dot_gap;

    // Draw the two dots
    gfx->fillRect(x, top_dot_y, separator_width, dot_height, COLON_COLOR);
    gfx->fillRect(x, bottom_dot_y, separator_width, dot_height, COLON_COLOR);
}


// ======================================================================
// MODIFIED: Update ONLY the Seconds Display (uses stored coords)
// ======================================================================
void updateSecondsDisplay(struct tm &timeinfo) {
  // Use stored coordinates (seconds_x, seconds_y, seconds_w, seconds_h)
  // These are calculated in updateHourMinuteDisplay
  if (seconds_w == 0) return; // Don't draw if coordinates not calculated yet

  strftime(seconds_buffer, sizeof(seconds_buffer), "%S", &timeinfo);

  gfx->setTextSize(9);
  gfx->setTextColor(TIME_COLOR, BG_COLOR);

  // Clear only the seconds area
  gfx->fillRect(seconds_x, seconds_y, seconds_w + 5, seconds_h + 5, BG_COLOR); // Use stored w/h + padding

  // Draw the new seconds
  gfx->setCursor(seconds_x, seconds_y);
  gfx->print(seconds_buffer);
}


// ======================================================================
// MODIFIED: Uses new CLOCK_START_Y reference for positioning
// ======================================================================
void updateWeatherDisplay() {
  // Calculate Y positions based on the new clock Y position
  int date_day_y = CLOCK_START_Y + 80;
  int middle_y_start = date_day_y + 35; // Position below date/day line

  int icon_x = 30; int icon_y = middle_y_start;
  int icon_w = 100; int icon_h = 100;
  int text_x_start = icon_x + icon_w + 30;

  // Clear only the weather area below the date/day line
  gfx->fillRect(0, middle_y_start - 5, gfx->width(), gfx->height() - middle_y_start + 5, BG_COLOR);


  String bmpPath = "/icons/" + weather_icon_code + ".bmp";
  Serial.print("Drawing icon (Weather Update): "); Serial.println(bmpPath);
  if (!drawBmpFromFile(SPIFFS, bmpPath.c_str(), icon_x, icon_y)) {
      Serial.println("BMP Draw Failed. Drawing placeholder.");
      gfx->drawRect(icon_x, icon_y, icon_w, icon_h, ICON_RECT_COLOR);
  }

  gfx->setTextSize(6); gfx->setTextColor(TEMP_COLOR, BG_COLOR);
  char temp_str[10]; dtostrf(current_temp, 3, 0, temp_str);
  int temp_x = text_x_start; int temp_y = icon_y + 20;
  gfx->setCursor(temp_x, temp_y); gfx->print(temp_str);
  gfx->setTextSize(3); gfx->setCursor(gfx->getCursorX() + 5, gfx->getCursorY() + 5); gfx->print("C");

  gfx->setTextSize(3); gfx->setTextColor(MINMAX_COLOR, BG_COLOR);
  char minmax_str[20]; sprintf(minmax_str, "%.0f/%.0f C", temp_min, temp_max);
  int minmax_x = temp_x; int minmax_y = temp_y + 60;
  gfx->setCursor(minmax_x, minmax_y); gfx->print(minmax_str);

  gfx->setTextSize(3); gfx->setTextColor(DESC_COLOR, BG_COLOR);
  int desc_x = icon_x; int desc_y = icon_y + icon_h + 15;
  gfx->setCursor(desc_x, desc_y); gfx->print(weather_description);

  gfx->setTextSize(3); gfx->setTextColor(HUMID_COLOR, BG_COLOR);
  char humid_str[15]; sprintf(humid_str, "Nem: %d%%", current_humidity);
  int humid_x = minmax_x; int humid_y = minmax_y + 35;
  gfx->setCursor(humid_x, humid_y); gfx->print(humid_str);
}


// BMP Drawing Helper Functions (Original) - Remain the same
uint16_t read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}
uint32_t read32(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}
bool drawBmpFromFile(fs::FS &fs, const char *filename, int16_t x, int16_t y) {
    File bmpFile;
    int bmpWidth, bmpHeight; uint8_t bmpDepth; uint32_t bmpImageoffset;
    uint32_t rowSize; uint8_t sdbuffer[3 * BUFFPIXEL];
    uint8_t buffidx = sizeof(sdbuffer); bool goodBmp = false; bool flip = true;
    int w, h, row, col; uint8_t r, g, b; uint32_t pos = 0;
    if ((x >= gfx->width()) || (y >= gfx->height())) return false;
    bmpFile = fs.open(filename, "r");
    if (!bmpFile) { Serial.print("BMP File not found: "); Serial.println(filename); return false; }
    if (read16(bmpFile) == 0x4D42) { read32(bmpFile); (void)read32(bmpFile); bmpImageoffset = read32(bmpFile);
        read32(bmpFile); bmpWidth = read32(bmpFile); bmpHeight = read32(bmpFile);
        if (read16(bmpFile) == 1) { bmpDepth = read16(bmpFile);
            if (((bmpDepth == 16) || (bmpDepth == 24)) && (read32(bmpFile) == 0)) {
                goodBmp = true;
                int bytesPerPixel = bmpDepth / 8;
                rowSize = (bmpWidth * bytesPerPixel + 3) & ~3;
                if (bmpHeight < 0) { bmpHeight = -bmpHeight; flip = false; }
                w = bmpWidth; h = bmpHeight;
                if ((x + w - 1) >= gfx->width())  w = gfx->width() - x;
                if ((y + h - 1) >= gfx->height()) h = gfx->height() - y;

                for (row = 0; row < h; row++) {
                    pos = bmpImageoffset + (flip ? (bmpHeight - 1 - row) : row) * rowSize;
                    if (bmpFile.position() != pos) { bmpFile.seek(pos); buffidx = sizeof(sdbuffer); }
                    for (col = 0; col < w; col++) {
                        if (buffidx >= sizeof(sdbuffer)) {
                            bmpFile.read(sdbuffer, sizeof(sdbuffer));
                            buffidx = 0; }
                        uint16_t color;
                        if (bmpDepth == 16) {
                           color = sdbuffer[buffidx] | (sdbuffer[buffidx + 1] << 8);
                            buffidx += 2;
                        } else {
                           b = sdbuffer[buffidx++]; g = sdbuffer[buffidx++]; r = sdbuffer[buffidx++];
                           color = gfx->color565(r, g, b);
                        }
                        gfx->drawPixel(x + col, y + row, color);
                    } // end col
                } // end row
            } else { Serial.printf("Unsupported BMP depth (%d) or compression.\n", bmpDepth); }
        } else { Serial.println("BMP format error (Planes!=1)."); }
    } else { Serial.println("Not a BMP file."); }
    bmpFile.close();
    return goodBmp;
}
