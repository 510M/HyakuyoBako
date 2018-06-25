extern "C" {
#include <user_interface.h>
};
#include <ESP8266WiFi.h>
#include <time.h>
#include <Wire.h> //AM2321

#include "define.h" // Git管理対象外とする！
#define JST   3600*9
#include "tools.h"

WiFiClientSecure client;

void setup() {
  
  Serial.begin(115200);
  delay(500);
  bool first;

  //External System
  //Deep-Sleep Wake

  if (ESP.getResetReason() == "Deep-Sleep Wake") {
    first = false;
  } else {
    first = true;
  }

  static const uint32_t USER_DATA_ADDR = 65; // uint32_t => 4バイトの符号なし整数
  // ↑64からだとstackとか表示されて止まってしまう
  // 4バイト区切り、0 ~ 63で256バイトなので本来は64から使用できるはず・・・
  
  // RTCメモリに最小限に記録することを考える
  // 1529593225,24.6,42.3,1024
  
  // データ情報   8バイト
  // データ総容量 504バイト
  // hash（FNV-1E） => uint32_t型　4 byte (32bit）
  // cnt (0to9.. 送信失敗した場合は最大XXまで) => unsigned short型　2 byte (16bit）65535まで
  // 512 - 4 - 8 = 500, 500 / 20 = 25
  // ※先頭位置を(65からに)1つ下げた分、4バイト差し引かなければならなかった
  // データ本体    合計20バイト x 31 = 496
  // crc（-40.0 to 80.0）=> boolean型 1byte
  // epoch（UNIXTIME） => time_t型　4 byte
  // temp（-40.0 to 80.0）=> float型 4 byte
  // humid（0 to 99.9）=> float型 4 byte
  // lum（0 to 1024）unsigned short型 2 byte 65535まで
  
  struct hyakuyo {
    time_t epoch;
    bool crc;
    float temp;
    float humid;
    unsigned short lum;
  };
  struct {
    unsigned long int hash;
    unsigned short cnt;
    struct hyakuyo data[25];
  }hyakuyo_t;

  if (first) {
    hyakuyo_t.cnt = 0;
  } else {
    if (system_rtc_mem_read(USER_DATA_ADDR, &hyakuyo_t, sizeof(hyakuyo_t))) {
      //Serial.println("system_rtc_mem_read success");

    } else {
      Serial.println("<<< system_rtc_mem_read faild>>>");
    }
    if (hyakuyo_t.cnt == 9) {
      
      hyakuyo_t.cnt = 0;
    } else {
      hyakuyo_t.cnt++;
    }
  }

  Wire.begin();

  // WiFi設定
  WiFi.setOutputPower(0); // 低出力に（節電！）20.5dBm(最大)から0.0dBm(最小)までの値
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

  //time_t epoch;
  struct tm *tm;
  static const char *wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thr", "Fri", "Sat"};

  
  Serial.println("\n【time_t型のサイズは " + String(sizeof(hyakuyo_t.data[hyakuyo_t.cnt].epoch)) + " バイトです。】\n");

  hyakuyo_t.data[hyakuyo_t.cnt].epoch = time(NULL);
  tm = localtime(&hyakuyo_t.data[hyakuyo_t.cnt].epoch);
  Serial.println(String(hyakuyo_t.data[hyakuyo_t.cnt].epoch));
  Serial.printf("%04d/%02d/%02d(%s) %02d:%02d:%02d\n",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                wd[tm->tm_wday],
                tm->tm_hour, tm->tm_min, tm->tm_sec);

  // ISO 8601 日本標準時(JST)

  //String D = String(tm->tm_year+1900) + "-" + String(tm->tm_mon+1) + "-" + String(tm->tm_mday);
  //D += "T" + String(tm->tm_hour) + ":" + String(tm->tm_min) + ":" + String(tm->tm_sec) + "%2B09:00";

  char D[28]; // 27文字（2018-06-21T02:30:26%2B09:00）＋ 末尾のNULL(\0)
  sprintf(D, "%04d-%02d-%02dT%02d:%02d:%02d%%2B09:00"
          , tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

  // 一応バッファを25(24+1) x 20回分とする
  // 10回（10分）毎に送信し、エラー時再試行+10回（10分まで）可能とする

  // センサーの初期化
  byte rdptr[20];
  readAM2321(rdptr, 8);

  //Serial.print("\n機能コード。3になってる？＞" + String(rdptr[0], HEX) + "\n");
  //Serial.print("\nバイト数。4になってる？＞"+ String(rdptr[1], HEX) + "\n");
  //Serial.print("\nCRC ＞"+ String(rdptr[7], HEX) + String(rdptr[6], HEX) + "\n");
  char b[100];
  sprintf(b, "\n機能コード:%02x, バイト数:%02x, CRC:%02x%02x\n", rdptr[0], rdptr[1], rdptr[7], rdptr[6]);
  Serial.print(b);
  // 排他的論理和＝どちらか一方が１のときのみ１ 両方１や両方０は０
  sprintf(b, "\n%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n", rdptr[0], rdptr[1], rdptr[2], rdptr[3], rdptr[40], rdptr[5], rdptr[6], rdptr[7]);
  Serial.print(b);


  sprintf(b, "\n計算:%04x\n", crc16(rdptr, 8));
  Serial.print(b);

  // 65535はエラー？
  Serial.println("\n【float型のサイズは " + String(sizeof(hyakuyo_t.data[hyakuyo_t.cnt].temp)) + " バイトです。】\n");
   if(crc16(rdptr, 8) == 0) {
    // CRC OK
    hyakuyo_t.data[hyakuyo_t.cnt].crc = true;
    
    if (rdptr[4] < B10000000) {
      hyakuyo_t.data[hyakuyo_t.cnt].temp = (float)(rdptr[4] * 256 + rdptr[5]) / 10.0;  // -40.0 to 80.0
    } else {
      // マイナス温度対策 ADD A_GOTO
      // 最上位ビット分引いてマイナスをつける
      rdptr[4] -= B10000000;
      hyakuyo_t.data[hyakuyo_t.cnt].temp = (float) - (rdptr[4] * 256 + rdptr[5]) / 10.0; // -40.0 to 80.0
    }
  
    hyakuyo_t.data[hyakuyo_t.cnt].humid = (float)(rdptr[2] * 256 + rdptr[3]) / 10.0;  // -40.0 to 80.0
  } else {
    // CRC NG
    hyakuyo_t.data[hyakuyo_t.cnt].crc = false;
    // とりま0をセット
    hyakuyo_t.data[hyakuyo_t.cnt].temp = 0;
    hyakuyo_t.data[hyakuyo_t.cnt].humid = 0;
  }

  hyakuyo_t.data[hyakuyo_t.cnt].lum = analogRead(0);                     // 0 to 1024 (ESP8266)
  Serial.println("\n【unsigned short型のサイズは " + String(sizeof(hyakuyo_t.data[hyakuyo_t.cnt].lum)) + " バイトです。】\n");
  Serial.print(hyakuyo_t.data[hyakuyo_t.cnt].temp, 1);
  Serial.print("°C");
  Serial.print("\t");
  Serial.print(hyakuyo_t.data[hyakuyo_t.cnt].humid, 1);
  Serial.print("%RH");
  Serial.print("\t");
  Serial.println(hyakuyo_t.data[hyakuyo_t.cnt].lum, DEC);

  Serial.println("\n<<< " + String(hyakuyo_t.cnt) + " >>>");

  // ここでhash計算予定
  hyakuyo_t.hash = 0;       // とりま0
  //hyakuyo_t.cnt           // 頭でセット済

  if (system_rtc_mem_write(USER_DATA_ADDR, &hyakuyo_t, sizeof(hyakuyo_t))) {
    Serial.println("system_rtc_mem_write success");
  } else {
    Serial.println("system_rtc_mem_write failed");
  }

  if(hyakuyo_t.cnt == 9) {
    // 10回目終了時、今までのデータをRTCメモリから全て表示してみる
    Serial.println("\n");
    for (int i = 0; i <= 9; i++) {
      char str[100];
      //sprintf(str, "\t【%d回目】UNIXタイム:%10d, 気温:%5.1f, 湿度:%4.1f, 明るさ:%4d",
      sprintf(str, "\t【%d回目】UNIXタイム:%10d, CRC:%s, 気温:%5.1f, 湿度:%4.1f, 明るさ:%4d",
          i+1,
          hyakuyo_t.data[i].epoch,
          (hyakuyo_t.data[i].crc ? "true" : "false"),
          hyakuyo_t.data[i].temp,
          hyakuyo_t.data[i].humid,
          hyakuyo_t.data[i].lum);
  
      Serial.println(str);
    }
    Serial.println("\n");
  }
  
  
  
  
  
  
  
  
  
  hyakuyo_t.data[hyakuyo_t.cnt].epoch = time(NULL);
   
  // いったんRTCメモリに保存することを想定したデータを作成してみる
  char data[25];
  sprintf(data, "%02d%10d%5.1f%4.1f%4d",
          hyakuyo_t.cnt,
          hyakuyo_t.data[hyakuyo_t.cnt].epoch,
          hyakuyo_t.data[hyakuyo_t.cnt].temp,
          hyakuyo_t.data[hyakuyo_t.cnt].humid,
          hyakuyo_t.data[hyakuyo_t.cnt].lum);
  
  String url = "/hyakuyobako/receive.php";
  //url += "?data=" + String(data);

  
  url += "?data=" + URLEncode(data);

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


