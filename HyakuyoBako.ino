#include <ESP8266WiFi.h>
#include <time.h>
#include "define.h" // Git管理対象外とする！

//AM2321
#include <Wire.h>

#define JST   3600*9

WiFiClientSecure client;

void setup() {

  Serial.begin(115200);
  delay(20);
  Serial.println("");
  Serial.println("START");
  
  String resetReason = ESP.getResetReason();
  Serial.println(resetReason);

  Wire.begin();



  // WiFi設定
  WiFi.setOutputPower(0); // 低出力に（節電！）
  WiFi.mode(WIFI_STA);
  WiFi.config(IPAddress(LOCAL), IPAddress(GATAWAY), IPAddress(SUBNET), IPAddress(DNS));


  // Wi-Fi接続
  WiFi.begin(SSID, PWD);

  while (WiFi.status() != WL_CONNECTED) { // Wi-Fi AP接続待ち
    delay(500);
    Serial.print(".");
  }

  // WiFiClient client;
  if (!client.connect(HOST, PORT)) {
    Serial.println("connection failed");
    return;
  }

  // NTP設定
  configTime(JST, 0, NTP1, NTP2);
  delay(500);


  time_t t;
  struct tm *tm;
  static const char *wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thr", "Fri", "Sat"};

  t = time(NULL);
  tm = localtime(&t);
  Serial.println(String(t));
  Serial.printf("%04d/%02d/%02d(%s) %02d:%02d:%02d\n",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                wd[tm->tm_wday],
                tm->tm_hour, tm->tm_min, tm->tm_sec);

  // ISO 8601 日本標準時(JST)

  //String D = String(tm->tm_year+1900) + "-" + String(tm->tm_mon+1) + "-" + String(tm->tm_mday);
  //D += "T" + String(tm->tm_hour) + ":" + String(tm->tm_min) + ":" + String(tm->tm_sec) + "%2B09:00";
  long E = t;
  char D[28]; // 27文字（2018-06-21T02:30:26%2B09:00）＋ 末尾のNULL(\0)
  sprintf(D, "%04d-%02d-%02dT%02d:%02d:%02d%%2B09:00"
          , tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);


  // センサーの初期化
  byte rdptr[20];
  readAM2321(rdptr, 8);

  float T = (float)(rdptr[4] * 256 + rdptr[5]) / 10.0;
  float H = (float)(rdptr[2] * 256 + rdptr[3]) / 10.0;

  Serial.print(T, 1);
  Serial.print("°C");
  Serial.print("\t");
  Serial.print(H, 1);
  Serial.print("%");
  Serial.print("\t");
  int L = analogRead(0);
  Serial.println(L, DEC);

  t = time(NULL);
  tm = localtime(&t);
  Serial.println(String(t));
  Serial.printf("%04d/%02d/%02d(%s) %02d:%02d:%02d\n",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                wd[tm->tm_wday],
                tm->tm_hour, tm->tm_min, tm->tm_sec);
  char S[28]; // 27文字（2018-06-21T02:30:26%2B09:00）＋ 末尾のNULL(\0)
  sprintf(S, "%04d-%02d-%02dT%02d:%02d:%02d%%2B09:00"
          , tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
  String data = "{";
  data += "\"send\":\"" + String(S) + "\",";
  data += "\"data\":[{";

  data += "\"id\":\"" + String(1) + "\",";
  data += "\"iso8601\":\"" + String(D) + "\",";
  data += "\"epoch\":\"" + String(E) + "\",";
  data += "\"temp\":\"" + String(T) + "\",";
  data += "\"humid\":\"" + String(H) + "\",";
  data += "\"lum\":\"" + String(L) + "\",";

  data += "}]";
  data += "}";

  String url = "/hyakuyobako/receive.php";
  url += "?data=" + data;

  Serial.println(url);
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + HOST + "\r\n" +
               "User-Agent: ESP8266\r\n" +
               "Pragma: no-cache\r\n" +
               "Connection: close\r\n\r\n");
               
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }

  // Read all the lines of the reply from server and print them to Serial
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      Serial.println("headers received");
      break;
    }
  }

  String line = client.readStringUntil('\n');
  if (line.startsWith("{\"state\":\"success\"")) {
    Serial.println("esp8266/Arduino CI successfull!");
    Serial.println("#####################################");
    Serial.println(line);
    Serial.println("#####################################");

  } else {
    Serial.println(line);
    Serial.println("esp8266/Arduino CI has failed");
  }

  // Ambientの初期化
  // センサー値の取得
  // Ambientへの送信
  
  WiFi.mode(WIFI_OFF);
  delay(20);
  WiFi.forceSleepBegin();

  Serial.println("SLEEP");
  delay(20);
  ESP.deepSleep(1e6 * 15, WAKE_RF_DEFAULT); // sleep 15 seconds
  delay(1000);
}

void loop() {

}

void readAM2321(byte *rdptr, byte length )
{
  int i;
  byte  deviceaddress = 0x5C;
  //step1
  Wire.beginTransmission(deviceaddress);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(1);
  //step2
  Wire.beginTransmission(deviceaddress);
  Wire.write( 0x03);
  Wire.write(0x00);
  Wire.write(0x04);
  Wire.endTransmission();
  delay(2);
  //step3
  Wire.requestFrom(deviceaddress, length);
  delayMicroseconds(60);
  if (length <= Wire.available())
  {
    for (i = 0; i < length; i++)
    {
      rdptr[i] = Wire.read();
    }
  }
}

