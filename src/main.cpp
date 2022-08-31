/* Initialization */
//Libraries
#include <Arduino.h>
#include <DMD32.h>
#include "fonts/SystemFont5x7.h"
#include "fonts/Arial_black_16.h"
#include <EEPROM.h>
#include <Ds1302.h>
#include "BluetoothSerial.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif
#if !defined(CONFIG_BT_SPP_ENABLED)
#error Serial Bluetooth not available or not enabled. It is only available for the ESP32 chip.
#endif

//Constants
#define DISPLAYS_ACROSS 2
#define DISPLAYS_DOWN 1
#define TIMER_MS 2000
#define FREQ  5000
#define CHANNEL 0
#define RESOLUTION 8
#define BLUETOOTH_NAME "p10Controller"

//Global Vars
String run_text, onair_text, running_text;
unsigned int txt_brightness, txt_speed, timer_, end_time, end_time_second, start_time = 0;
bool is_timer = false, is_tiner = false, is_finished = false, confirmRequestPending = true, clear_screen = false, is_full_clock = false;
char onair_txt[24], clock_buffer[15];

//Classic Bluetooth
BluetoothSerial SerialBT;

// DS1302 RTC
Ds1302 rtc(25, 32, 33); //RST, CLK, DATA

//DMD
DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);

//Timer Setup
hw_timer_t * disp_timer = NULL;

/* Methods */
String EEPROM_get(char add)
{
  char data[100]; //Max 100 Bytes
  int len = 0;
  unsigned char k;
  k = EEPROM.read(add);
  while (k != '\0' && len < 500) //Read until null character
  {
    k = EEPROM.read(add + len);
    data[len] = k;
    len++;
  }
  data[len] = '\0';
  return String(data);
}

String secondsToHMS(const uint32_t seconds)
{
    uint32_t t = seconds;
    uint16_t h,m,s;
    String h_,m_,s_;

    s = t % 60;
    if(s < 10)
    {
      s_ = "0" + String(s);
    }
    else
    {
      s_ = String(s);
    }

    t = (t - s)/60;
    m = t % 60;
    if(m < 10)
    {
      m_ = "0" + String(m);
    }
    else
    {
      m_ = String(m);
    }

    t = (t - m)/60;
    h = t;
    if(h < 10)
    {
      h_ = "0" + String(h);
    }
    else
    {
      h_ = String(h);
    }

    return h_ + m_ + s_;
}

uint8_t parseDigits(char* str, uint8_t count)
{
    uint8_t val = 0;
    while(count-- > 0) val = (val * 10) + (*str++ - '0');
    return val;
}

void setBrightness()
{
  //control the brightness of LED
  ledcSetup(CHANNEL, FREQ, RESOLUTION); 
  //attach the channel to the GPIO to be controlled
  ledcAttachPin(PIN_DMD_nOE, CHANNEL);
  ledcWrite(CHANNEL, txt_brightness);
}

void IRAM_ATTR triggerScan()
{
  dmd.scanDisplayBySPI();
  setBrightness();
}

void initTimer()
{
  //return the clock speed of the CPU
  uint8_t cpuClock = ESP.getCpuFreqMHz();  
  //display timer
  disp_timer = timerBegin(1, cpuClock, true);
  timerAttachInterrupt(disp_timer, &triggerScan, true);
  timerAlarmWrite(disp_timer, TIMER_MS, true);
  // start an alarm 
  timerAlarmEnable(disp_timer);
}

void BTConfirmRequestCallback(uint32_t numVal)
{
  confirmRequestPending = true;
  timerDetachInterrupt(disp_timer);
  delay(6);
}

void BTAuthCompleteCallback(boolean success)
{
  confirmRequestPending = false;
  if(success)
  {
    initTimer();
  }
  else
  {
    initTimer();
  }
}

void EEPROM_put(char add, String data)
{
  int _size = data.length();
  int i;
  for (i = 0; i < _size; i++)
  {
    EEPROM.write(add + i, data[i]);
  }
  EEPROM.write(add + _size, '\0'); //Add termination null character for String Data
  timerDetachInterrupt(disp_timer);
  delay(6);
  EEPROM.commit();
  initTimer();
}

void startCountdown()
{
  end_time = start_time;
  start_time = end_time-(millis()-timer_);
  end_time_second = start_time/1000;

}

void initVars()
{
  run_text = EEPROM_get(10) + " ";
  txt_speed = EEPROM_get(100).toInt();
  txt_brightness = EEPROM_get(110).toInt();
  onair_text = EEPROM_get(120);
  onair_text.toCharArray(onair_txt, 24);
}

void getBluetoothData()
{
  while (SerialBT.available() > 0) {
    char c = SerialBT.read();
    running_text += c;
  }
  running_text.trim();
  if(running_text.length() > 0 && running_text.indexOf("1234_") >= 0) { 
    if(running_text.indexOf("STXT_") >= 0)
    {
      SerialBT.println("OK.Running text has been modified");
      EEPROM_put(10, running_text.substring(10,running_text.length()));
      is_finished = true;
      clear_screen = true;
    }
    else if(running_text.indexOf("STMR_") >= 0)
    {
      SerialBT.println("OK.Timer has been started");
      start_time = (running_text.substring(10,running_text.length()).toInt()*1000)+1000;
      is_finished = true;
      is_timer = true;
      clear_screen = true;
    }
    else if(running_text.indexOf("STNR_") >= 0)
    {
      SerialBT.println("OK.Full screen timer has been started");
      start_time = (running_text.substring(10,running_text.length()).toInt()*1000)+1000;
      is_finished = true;
      is_tiner = true;
      clear_screen = true;
    }
    else if(running_text.indexOf("SCLK_") >= 0)
    {
      SerialBT.println("OK.Time has been adjusted");
      running_text.substring(10,running_text.length()).toCharArray(clock_buffer, 15);
      Ds1302::DateTime dt;
      dt.year = parseDigits(clock_buffer, 2);
      dt.month = parseDigits(clock_buffer + 2, 2);
      dt.day = parseDigits(clock_buffer + 4, 2);
      dt.dow = parseDigits(clock_buffer + 6, 1);
      dt.hour = parseDigits(clock_buffer + 7, 2);
      dt.minute = parseDigits(clock_buffer + 9, 2);
      dt.second = parseDigits(clock_buffer + 11, 2);
      // set the date and time
      rtc.setDateTime(&dt);
      is_finished = true;
      clear_screen = true;
    }
    else if(running_text.indexOf("SSPD_") >= 0)
    {
      SerialBT.println("OK.Speed has been changed");
      EEPROM_put(100, running_text.substring(10,running_text.length()));
      is_finished = true;
      clear_screen = true;
    }
    else if(running_text.indexOf("SBRT_") >= 0)
    {
      SerialBT.println("OK.Brightness has been adjusted");
      EEPROM_put(110, running_text.substring(10,running_text.length()));
      clear_screen = true;
    }
    else if(running_text.indexOf("STBR_") >= 0)
    {
      SerialBT.println("OK.Broadcast text has been changed");
      EEPROM_put(120, running_text.substring(10,running_text.length()));
      is_finished = true;
      clear_screen = true;
    }
    else if(running_text.indexOf("FCLK") >= 0)
    {
      SerialBT.println("OK.Full clock mode has been chosen");
      is_finished = true;
      clear_screen = true;
      is_full_clock = true;
    }
    else
    {
      SerialBT.println("NOK.No such command");
    }
  }
  else if(running_text.length() > 0 && running_text.indexOf("1234_") < 0)
  {
    SerialBT.println("NOK.Auth is failed");
  }
  running_text = "";
}

void showClockTimer(String uiTime, byte bColonOn)
{
  dmd.drawChar(10, 0, '0'+((uiTime.toInt()%1000000)/100000), GRAPHICS_NORMAL);   // thousands
  dmd.drawChar(17, 0, '0'+((uiTime.toInt()%100000)/10000), GRAPHICS_NORMAL);   // hundreds
  dmd.drawChar(26, 0, '0'+((uiTime.toInt()%10000)/1000), GRAPHICS_NORMAL);   // tens
  dmd.drawChar(33, 0, '0'+ ((uiTime.toInt()%1000)/100), GRAPHICS_NORMAL);   // units
  dmd.drawChar(42, 0, '0'+((uiTime.toInt()%100)/10), GRAPHICS_NORMAL);   // thousands
  dmd.drawChar(49, 0, '0'+(uiTime.toInt()%10), GRAPHICS_NORMAL);   // hundreds
  if(bColonOn)
  {
    dmd.drawChar(22, 0, ':', GRAPHICS_OR);   // clock colon overlay on
    dmd.drawChar(38,  0, ':', GRAPHICS_OR);   // clock colon overlay on
  }
  else
  {
    dmd.drawChar(22, 0, ':', GRAPHICS_NOR);   // clock colon overlay off
    dmd.drawChar(38,  0, ':', GRAPHICS_NOR);   // clock colon overlay on
  }
}

void showClockTiner(String uiTime, byte bColonOn)
{
  dmd.drawChar(1, 1, '0'+((uiTime.toInt()%1000000)/100000), GRAPHICS_NORMAL);   // thousands
  dmd.drawChar(10, 1, '0'+((uiTime.toInt()%100000)/10000), GRAPHICS_NORMAL);   // hundreds
  dmd.drawChar(23, 1, '0'+((uiTime.toInt()%10000)/1000), GRAPHICS_NORMAL);   // tens
  dmd.drawChar(32, 1, '0'+ ((uiTime.toInt()%1000)/100), GRAPHICS_NORMAL);   // units
  dmd.drawChar(45, 1, '0'+((uiTime.toInt()%100)/10), GRAPHICS_NORMAL);   // thousands
  dmd.drawChar(54, 1, '0'+(uiTime.toInt()%10), GRAPHICS_NORMAL);   // hundreds
  if(bColonOn)
  {
    dmd.drawChar(19, 0, ':', GRAPHICS_OR);   // clock colon overlay on
    dmd.drawChar(41,  0, ':', GRAPHICS_OR);   // clock colon overlay on
  }
  else
  {
    dmd.drawChar(19, 0, ':', GRAPHICS_NOR);   // clock colon overlay on
    dmd.drawChar(41,  0, ':', GRAPHICS_NOR);   // clock colon overlay on
  }
}

void initFont(int fnt)
{
  if(fnt == 0)
  {
    dmd.selectFont(SystemFont5x7);
  }
  else if(fnt == 1)
  {
    dmd.selectFont(Arial_Black_16);
  }
}

void softwareReset()
{
  while (SerialBT.available() > 0) {
    char c = SerialBT.read();
    running_text += c;
  }
  running_text.trim();
  if(running_text.length() > 0 && running_text.indexOf("1234_") >= 0) { 
    if(running_text.indexOf("FRST") >= 0)
    {
      SerialBT.println("OK.Reset has been successful");
      is_timer = false;
      is_tiner = false;
      is_full_clock = false;
      start_time = 10;
      clear_screen = true;
    }
    else if(running_text.indexOf("STMR_") >= 0)
    {
      SerialBT.println("OK.Timer has been started");
      start_time = (running_text.substring(10,running_text.length()).toInt()*1000)+1000;
      is_finished = true;
      is_tiner = false;
      is_timer = true;
      is_full_clock = false;
      clear_screen = true;
    }
    else if(running_text.indexOf("STNR_") >= 0)
    {
      SerialBT.println("OK.Full screen timer has been started");
      start_time = (running_text.substring(10,running_text.length()).toInt()*1000)+1000;
      is_finished = true;
      is_timer = false;
      is_tiner = true;
      is_full_clock = false;
      clear_screen = true;
    }
    else if(running_text.indexOf("STOO") >= 0)
    {
      SerialBT.println("OK.Timer has been set to 0");
      start_time = 1000;
      is_finished = true;
      is_timer = true;
      is_tiner = true;
    }
    else if(running_text.indexOf("STBR_") >= 0)
    {
      SerialBT.println("OK.Broadcast text has been changed");
      EEPROM_put(120, running_text.substring(10,running_text.length()));
      is_finished = true;
      is_timer = true;
      is_tiner = true;
    }
    else if(running_text.indexOf("FCLK") >= 0)
    {
      SerialBT.println("OK.Full clock mode has been chosen");
      is_finished = true;
      clear_screen = true;
      is_full_clock = true;
      is_tiner = false;
      is_timer = false;
    }
  }
  else if(running_text.length() > 0 && running_text.indexOf("1234_") < 0)
  {
    SerialBT.println("NOK.Auth is failed");
  }
  running_text = "";
}

void initSerial()
{
  Serial.begin(115200);
  SerialBT.enableSSP();
  SerialBT.onConfirmRequest(BTConfirmRequestCallback);
  SerialBT.onAuthComplete(BTAuthCompleteCallback);
  SerialBT.begin(BLUETOOTH_NAME); //Bluetooth device name
}

void confirmBluetoothPending()
{
  if(confirmRequestPending)
  {
    SerialBT.confirmReply(true);
  }
}

void normalMode()
{
  int length = run_text.length() + 1;
  char display[length];
  run_text.toCharArray(display, length);
  initFont(0);
  if(clear_screen == true)
  {
    dmd.clearScreen(true);
    clear_screen = false;
  }
  dmd.drawMarquee(display, length, 32*DISPLAYS_ACROSS, 9);
  long timerms = millis();
  is_finished = false;
  while (!is_finished) {
    if ((timerms + txt_speed) < millis()) {
      is_finished = dmd.stepMarquee(-2,0);
      getBluetoothData();
      // get the current time
      Ds1302::DateTime now;
      rtc.getDateTime(&now);
      static uint8_t last_second = 0;
      if (last_second != now.second)
      {
        initVars();
        last_second = now.second;
        showClockTimer(secondsToHMS((now.hour*3600)+(now.minute*60)+now.second), true);
      }
      timerms = millis();
    }
  }
  // get the current time
  Ds1302::DateTime now;
  rtc.getDateTime(&now);
  static uint8_t last_second = 0;
  if (last_second != now.second)
  {
    initVars();
    last_second = now.second;
    showClockTimer(secondsToHMS((now.hour*3600)+(now.minute*60)+now.second), true);
  }
}

void timerMode()
{
  while(is_timer)
  {
    initVars();
    softwareReset();
    initFont(0);
    dmd.clearScreen(true);
    showClockTimer(secondsToHMS(end_time_second), true);
    dmd.drawString((64-(onair_text.length()*6))/2,  9, onair_txt, onair_text.length(), GRAPHICS_NORMAL);
    timer_ = millis();
    delay(100);
    startCountdown();
    if(end_time_second==0)
    {
      start_time = 0;
      while(start_time == 0)
      {
        softwareReset();
        showClockTimer(secondsToHMS(0), true);
      }
    }
  }
}

void tinerMode()
{
  while(is_tiner)
  {
    softwareReset();
    initFont(1);
    dmd.clearScreen(true);
    showClockTiner(secondsToHMS(end_time_second), true);
    timer_ = millis();
    delay(100);
    startCountdown();
    if(end_time_second==0)
    {
      start_time = 0;
      while(start_time == 0)
      {
        softwareReset();
        showClockTiner(secondsToHMS(0), true);
      }
    }
  }
}

void fullClockMode()
{
  while(is_full_clock)
  {
    softwareReset();
    dmd.clearScreen(true);
    initFont(1);
    Ds1302::DateTime now;
    rtc.getDateTime(&now);
    showClockTiner(secondsToHMS((now.hour*3600)+(now.minute*60)+now.second), true);
    delay(500);
  }
}
/* Line 1 - Main Program */
/* Line 2 - Main Program */
/* Line 3 - Main Program */
/* Line 4 - Main Program */
/* Line 5 - Main Program */
/* Line 6 - Main Program */
void setup(void)
{
  initSerial();
  initTimer();
  EEPROM.begin(512);
  rtc.init();
}

void loop(void)
{
  confirmBluetoothPending();
  initVars();
  timerMode();
  tinerMode();
  fullClockMode();
  if(is_timer == false && is_tiner == false && is_full_clock == false)
  {
    normalMode();
  }
}