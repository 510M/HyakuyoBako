  static const int MAX_COUNT = 3;
  static const uint32_t USER_DATA_ADDR = 65; // uint32_t => 4バイトの符号なし整数

  // ↑64からだとstackとか表示されて止まってしまう


  // RTCメモリに最小限に記録することを考える
  // 1529593225,24.6,42.3,1024
  
  // データ総容量 488バイト
  
  // データ情報   8バイト
  // hash（FNV-1E） => unsigned long型　4 byte (32bit）
  // cnt (0to9.. 送信失敗した場合は最大25まで) => unsigned short型　2 byte (16bit）65535まで
  
  // データ本体   24バイト x 20 = 480バイト (512-8-4=500バイトまでしか使用できないため)
  // epoch （UNIXTIME マイクロ秒まで） => timeval型　8 byte (time_t 4byte + suseconds_t 4byte) (ミリ秒まで知りたいのでマイクロ秒も保持)
  // crc（-40.0 to 80.0）=> boolean型 1byte
  // temp（-40.0 to 80.0）=> float型 4 byte
  // humid（0 to 99.9）=> float型 4 byte
  // lum（0 to 1024）unsigned short型 2 byte 65535まで

struct Data {
  timeval epoch;
  bool crc;
  float temp;
  float humid;
  unsigned short lum;
};
struct Hyakuyo{
  unsigned long int hash;
  unsigned short cnt;
  struct Data data[20];
};
struct Hyakuyo hyakuyo;

// AM2321 Product Manualより

unsigned short crc16(unsigned char *ptr, unsigned char len)
{
  unsigned short crc = 0xFFFF;
  unsigned char i;
  while (len--)
  {

    
    crc ^= *ptr++;
    for (i = 0; i < 8; i++)
    {


      
      if (crc & 0x01)
      {
        crc >>= 1;
        crc ^= 0xA001;
      } else
      {
        crc >>= 1;
      }

    }
  }

  return crc;
}
void readAM2321(byte *rdptr, byte length ) {
  int i;
  byte  deviceaddress = 0x5C;

  //step1
  Wire.beginTransmission(deviceaddress);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(1);
  //step2
  Wire.beginTransmission(deviceaddress);
  Wire.write(0x03);
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

String URLEncode(const char* msg) {
  const char *hex = "0123456789abcdef";
  String encodedMsg = "";

  while (*msg != '\0') {
    if ( ('a' <= *msg && *msg <= 'z')
         || ('A' <= *msg && *msg <= 'Z')
         || ('0' <= *msg && *msg <= '9') ) {
      encodedMsg += *msg;
    } else {
      encodedMsg += '%';
      encodedMsg += hex[*msg >> 4];
      encodedMsg += hex[*msg & 15];
    }
    msg++;
  }
  return encodedMsg;
}

  void rtcInit(struct Hyakuyo *hyakuyo){

  //Serial.println("\nデータのサイズ" + String(sizeof(hyakuyo->data)) + "\n");
  //Serial.println("\nデータのサイズ" + String(sizeof(struct Data)) + "\n");
  
  int data_size = sizeof(hyakuyo->data) / sizeof(struct Data);
    hyakuyo->hash = 0;
    hyakuyo->cnt = 0;
    for (int i = 0; i < data_size; i++) {
      hyakuyo->data[i].epoch.tv_sec = 0;
      hyakuyo->data[i].epoch.tv_usec = 0;
      hyakuyo->data[i].crc = false;
      hyakuyo->data[i].temp = 0;
      hyakuyo->data[i].humid = 0;
    }
  
    //_hyakuyo.cnt = 0;
    //_hyakuyo = *hyakuyo;
    
    if (system_rtc_mem_write(USER_DATA_ADDR, &*hyakuyo, sizeof(*hyakuyo))) {
      Serial.println("RTC INIT success");
    } else {
      Serial.println("RTC INIT failed");
    }
  }
  
  String hyakuyoJSON(struct Hyakuyo _hyakuyo) {
     String str = "";
     char buf[100];
      struct tm *tm;
      //Serial.println("渡されたカウントは" + String(_hyakuyo.cnt) + "です。");    
      for (int i = 0; i <= _hyakuyo.cnt; i++) {
  
        char datetime[50];
        tm = localtime(&_hyakuyo.data[i].epoch.tv_sec);
        // “YYYY-MM-DD HH:mm:ss.sss”
        //strftime(buf,sizeof(datetime), "%Y-%m-%d %H:%M:%S", tm);
        strftime(datetime,sizeof(datetime), "%F %T", tm);

        //Serial.println("フォーマット前の日付は" + String(_hyakuyo.data[i].epoch.tv_sec) + "です。");
        /*
        sprintf(datetime, "%04d/%02d/%02d %02d:%02d:%02d\n",
                  tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                  tm->tm_hour, tm->tm_min, tm->tm_sec);
        */        
        //Serial.println("フォーマットした日付は" + String(datetime) + "です。");  
  
       
        sprintf(buf, "\t【%d回目】ISO8601:%s.%03d, CRC:%s, 気温:%5.1f, 湿度:%4.1f, 明るさ:%4d\n",
          i+1,
          datetime,
          (int)_hyakuyo.data[i].epoch.tv_usec/1000,
          (_hyakuyo.data[i].crc ? "true" : "false"),
          _hyakuyo.data[i].temp,
          _hyakuyo.data[i].humid,
          _hyakuyo.data[i].lum);
      str += String(buf);
    }
    return str;
}

