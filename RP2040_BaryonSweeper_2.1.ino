#include <Arduino.h>
#include <RP2040.h> // Adicionado para watchdog
#include "keys.h"
#include <string.h>
#include <AES.h>
#include <hardware/gpio.h> // Adicionado para gpio_pull_up

#if defined(ARDUINO_WAVESHARE_RP2040_ZERO)
#include <Adafruit_NeoPixel.h>
#define LED_PIN 16
Adafruit_NeoPixel pixels(1, LED_PIN, NEO_GRB + NEO_KHZ800);
#else
#define pin_debugLed 25
#endif

#define debugSerial Serial
#define pspSerial Serial1

#define TX_PIN 0  // Pino TX (GPIO0, saída)
#define RX_PIN 1  // Pino RX (GPIO1, entrada)

byte serial_code[8] = {0};
byte msg[64] = {0};
uint8_t msgLength;
byte version;
byte pspdata[4] = {0};
byte challenge1a[16] = {0};
byte challenge1b[16] = {0};
byte tempBuffer[64] = {0};

unsigned long ledTime = 0;
bool ledOn = 0;

void setLedCommunication(bool isCommunicating) {
#if defined(ARDUINO_WAVESHARE_RP2040_ZERO)
  if (isCommunicating) {
    pixels.setPixelColor(0, pixels.Color(0, 255, 0)); // Verde durante comunicação
  } else {
    // Volta para o estado normal (azul ou desligado, baseado em ledOn)
    if (ledOn) {
      pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Azul
    } else {
      pixels.setPixelColor(0, 0); // Desligado
    }
  }
  pixels.show();
#else
  if (isCommunicating) {
    digitalWrite(pin_debugLed, HIGH); // Liga
    delay(125); // Pisca por 125ms
    digitalWrite(pin_debugLed, LOW); // Desliga
  } else {
    // Volta para o estado normal baseado em ledOn
    digitalWrite(pin_debugLed, ledOn ? HIGH : LOW);
  }
#endif
}

void testLed()
{
  if (serial_code[3] == 0xFF)
  {
    //debugSerial.println(F("S"));
    if(millis() - ledTime > 1000)
    {
      ledTime = millis();
      ledOn = !ledOn;

#if defined(ARDUINO_WAVESHARE_RP2040_ZERO)
      if(ledOn)
        pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Azul para ligado normal
      else
        pixels.setPixelColor(0, 0);
      pixels.show();
#else
      digitalWrite(pin_debugLed, ledOn ? HIGH : LOW);
#endif
    }
  }
  else
  {
#if defined(ARDUINO_WAVESHARE_RP2040_ZERO)
    pixels.setPixelColor(0, 0);
    pixels.show();
#else
    digitalWrite(pin_debugLed, LOW);
#endif
    //debugSerial.println(F("N"));
  }
}

void setup()
{
  gpio_pull_up(RX_PIN); // Habilitar pull-up no pino RX (GPIO1, entrada)
  gpio_pull_up(TX_PIN); // Habilitar pull-up no pino TX (GPIO0, saída), opcional mas adicionado conforme pedido

  pspSerial.begin(19200, SERIAL_8E1);
  debugSerial.begin(115200);

#if defined(ARDUINO_WAVESHARE_RP2040_ZERO)
  pixels.begin();
  pixels.clear();
  pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Inicializa em azul
  pixels.show();
#else
  pinMode(pin_debugLed, OUTPUT);
  digitalWrite(pin_debugLed, HIGH);
#endif

  serial_code[0] = 0xA5;
  serial_code[1] = 0x06;
  serial_code[2] = 0x06;
  serial_code[3] = 0xFF;
  serial_code[4] = 0xFF;
  serial_code[5] = 0xFF;
  serial_code[6] = 0xFF;
  serial_code[7] = 0x52;

  debugSerial.println(F("Start service"));

  watchdog_enable(2000, 1); // Adicionado: Watchdog com 2s timeout
}

void log_byte(byte b)
{
  if (b < 0x10)
    debugSerial.print(F("0"));
  debugSerial.print(b, HEX);
  debugSerial.print(F(" "));
}

void psp_write(byte *b, uint8_t len, bool generateChecksum)
{
  byte tempCS[1] = {0};

  pspSerial.write(b, len);

  if (generateChecksum)
  {
    tempCS[0] = GeneratePacketChecksum(b, len);
    pspSerial.write(tempCS, 1);
  }

  debugSerial.print(F("Arduino: "));
  for (uint8_t i = 0; i < len; i++)
    log_byte(b[i]);
  if (generateChecksum)
    log_byte(tempCS[0]);
  debugSerial.println();
}

unsigned char GeneratePacketChecksum(byte *inputArray, uint8_t len) 
{
  uint8_t sum = 0;

  for (uint8_t i = 0; i < len; i++)
    sum += inputArray[i];

  return ((sum & 0xFF) ^ 0xFF);
}

void MixChallenge1(byte *data, byte version, byte *challenge)
{
  byte secret1[8] = {0};
  get_challenge1_secret(secret1, version);

  data[0] = secret1[0];
  data[4] = secret1[1];
  data[8] = secret1[2];
  data[0xC] = secret1[3];
  data[1] = secret1[4];
  data[5] = secret1[5];
  data[9] = secret1[6];
  data[0xD] = secret1[7];
  data[2] = challenge[0];
  data[6] = challenge[1];
  data[0xA] = challenge[2];
  data[0xE] = challenge[3];
  data[3] = challenge[4];
  data[7] = challenge[5];
  data[0xB] = challenge[6];
  data[0xF] = challenge[7];
}

void MixChallenge2(byte *data, byte version, byte *challenge)
{
  byte secret2[8] = {0}; 
  get_challenge2_secret(secret2, version);

  data[0] = challenge[0];
  data[4] = challenge[1];
  data[8] = challenge[2];
  data[0xC] = challenge[3];
  data[1] = challenge[4];
  data[5] = challenge[5];
  data[9] = challenge[6];
  data[0xD] = challenge[7];
  data[2] = secret2[0];
  data[6] = secret2[1];
  data[0xA] = secret2[2];
  data[0xE] = secret2[3];
  data[3] = secret2[4];
  data[7] = secret2[5];
  data[0xB] = secret2[6];
  data[0xF] = secret2[7];
}

void MatrixSwap(byte* data, uint8_t length)
{
  byte temp[length] = {0};
  byte newMap[length] = {0x00, 0x04, 0x08, 0x0C, 0x01, 0x05, 0x09, 0x0D, 0x02, 0x06, 0x0A, 0x0E, 0x03, 0x07, 0x0B, 0x0F};

  for (uint8_t i = 0; i < length; i++)
  {
    temp[i] = data[newMap[i]];
  }

  memcpy(data, temp, length);
}

void ecb_encrypt(byte* plain, byte* cipher, byte* key, int sizeKey)
{
  AES aes;

  aes.set_key(key, sizeKey);
  aes.encrypt(plain, cipher);
}

void do_aes_decrypt(byte* cipher, int size, byte* plain, byte* key)
{
  AES aes;
  byte dec_iv_B[16] = {0};

  aes.set_key(key, 16);
  aes.do_aes_decrypt(cipher, size, plain, key, 128, dec_iv_B);
}

void generateSysconResponses()
{
  version = msg[0];
  byte tempKey[16] = {0};
  byte req[msgLength - 1] = {0};
  byte data[16];
  byte second[16];

  get_keystore(tempKey, version);

  tempBuffer[0] = 0xA5;
  tempBuffer[1] = 0x12;
  tempBuffer[2] = 0x06;

  if (tempKey[0] == 0)
  {
    debugSerial.println(F("Key not found"));

    for (uint8_t i = 3; i < 19; i++)
      tempBuffer[i] = 0xFF;

    return;
  }

  for (uint8_t i = 1; i < msgLength; i++)
    req[i-1] = msg[i];

  MixChallenge1(data, version, req);
  MatrixSwap(data, 16);
  ecb_encrypt(data, challenge1a, tempKey, 16);
  memcpy(second, challenge1a, 16);
  ecb_encrypt(second, challenge1b, tempKey, 16);
  MatrixSwap(challenge1b, 16);
  memcpy(tempBuffer + 3, challenge1a, 8);
  memcpy(tempBuffer + 3 + 8, challenge1b, 8);
}

void loop()
{
  if (pspSerial.available() > 3)
  {
    // Início da comunicação: Piscar em verde (Zero) ou rápido 125ms (normal)
    setLedCommunication(true);

    pspdata[0] = pspSerial.read();

    debugSerial.print(F("PSP:     "));
    log_byte(pspdata[0]);

    if (pspdata[0] == 0x5A)
    {
      pspdata[1] = pspSerial.read(); // packet length
      pspdata[2] = pspSerial.read(); // operation code

      log_byte(pspdata[1]);
      log_byte(pspdata[2]);

      if (pspdata[1] == 0x02) // if packet has no message then byte after the opcode is the checksum
        msgLength = 0;
      else // packet contains a message
      {
        msgLength = ((uint8_t)pspdata[1] - 2); 

        unsigned long startTime = millis();
        while (pspSerial.available() < (uint8_t)pspdata[1] - 1 && millis() - startTime < 100)
        {
          delayMicroseconds(10);
        };

        if (millis() - startTime >= 100) {
          debugSerial.println(F("Timeout waiting for message bytes"));
          setLedCommunication(false);
          return;
        }

        for (uint8_t i = 0; i < msgLength; i++)
          msg[i] = pspSerial.read();

        debugSerial.print(F("( "));
        for (uint8_t i = 0; i < msgLength; i++)
          log_byte(msg[i]);
        debugSerial.print(F(") "));
      }

      pspdata[3] = pspSerial.read();

      log_byte(pspdata[3]);
      debugSerial.println();

      switch (pspdata[2])
      {

      case 0x01:
      {
        memcpy_P(tempBuffer, answer_01, 7);
        psp_write(tempBuffer, 7, false);
      }
      break;

      case 0x0C:
      {
        byte tempcs[1] = {0};

        for (uint8_t i = 0; i < 7; i++)
          tempBuffer[i] = serial_code[i];

        psp_write(tempBuffer, 7, true);
      }
      break;

      case 0x80:
      {
        byte tempCS[1] = {0};

        generateSysconResponses();
        psp_write(tempBuffer, 19, true);
      }
      break;

      case 0x81:
      {
        tempBuffer[0] = 0xA5;
        tempBuffer[1] = 0x12;
        tempBuffer[2] = 0x06;

        byte data2[16] = {0};
        byte tempKey[16] = {0};
        byte challenge2[16] = {0};
        byte response2[16] = {0};

        MixChallenge2(data2, version, challenge1b);
        MatrixSwap(data2, 16);
        get_keystore(tempKey, version);
        ecb_encrypt(data2, challenge2, tempKey, 128);
        ecb_encrypt(challenge2, response2, tempKey, 128);
        memcpy(tempBuffer + 3, response2, 16);
        psp_write(tempBuffer, 19, true);

        if (version == 0xEB || version == 0xB3)
        {
          memcpy_P(tempBuffer, psp_81, 4);
          psp_write(tempBuffer, 4, false);
          
          debugSerial.print(F("GO:      "));
          pspdata[0] = pspSerial.read();
          pspdata[1] = pspSerial.read();
          pspdata[2] = pspSerial.read();
          pspdata[3] = pspSerial.read();

          memcpy_P(tempBuffer, answer_01, 7);
          psp_write(tempBuffer, 7, false);
        }
      }
      break;

      case 0x90:
      {
        byte req[32] = {0};
        byte tempKey[16] = {0};
        byte payload[32] = {0};
        byte payload91[32] = {0};
        byte temp_go_secret[32] = {0};
        byte resp2[32] = {0};

        memcpy(req, msg + 8, 32); // screq[0x8:0x28]
        memcpy_P(tempKey, go_key1, 16);
        do_aes_decrypt(req, 32, payload, tempKey);
        memcpy(payload91, payload + 8, 8);    // payload[8:16]
        memcpy(payload91 + 8, payload, 8);    // payload[0:8]
        memset(payload91 + 16, 0, 16);        // bytearray(16)
        memcpy_P(temp_go_secret, go_secret, 16);

        for (uint8_t i = 16; i < 32; i++)
          if (payload[i] != temp_go_secret[i-16])
          {
            debugSerial.println(F("Invalid request from Syscon"));
            return;
          }

        debugSerial.println(F("Go Handshake Request is valid"));

        memcpy_P(tempKey, go_key2, 16);
        do_aes_decrypt(payload91, 32, resp2, tempKey);
        memcpy_P(tempBuffer, answer_90, 11);
        memcpy(tempBuffer + 11, resp2, 32);
        psp_write(tempBuffer, 43, true);
      }
      break;

      case 0x03:
      {
        memcpy_P(tempBuffer, answer_03, 6);
        psp_write(tempBuffer, 6, false);
      }
      break;

      case 0x07:
      {
        memcpy_P(tempBuffer, answer_07, 6);
        psp_write(tempBuffer, 6, false);
      }
      break;

      case 0x0B:
      {
        memcpy_P(tempBuffer, answer_0B, 6);
        psp_write(tempBuffer, 6, false);
      }
      break;

      case 0x09:
      {
        memcpy_P(tempBuffer, answer_09, 6);
        psp_write(tempBuffer, 6, false);
      }
      break;

      case 0x02:
      {
        memcpy_P(tempBuffer, answer_02, 5);
        psp_write(tempBuffer, 5, false);
      }
      break;

      case 0x04:
      {
        memcpy_P(tempBuffer, answer_04, 6);
        psp_write(tempBuffer, 6, false);
      }
      break;

      case 0x16:
      {
        memcpy_P(tempBuffer, answer_16, 21);
        psp_write(tempBuffer, 21, false);
      }
      break;

      case 0x0D:
      { 
        memcpy_P(tempBuffer, answer_0D, 9);
        psp_write(tempBuffer, 9, false);
      }
      break;

      case 0x08:
      {
        memcpy_P(tempBuffer, answer_08, 6);
        psp_write(tempBuffer, 6, false);
      }
      break;

      default:
      {
        debugSerial.print(F("No option selected "));
        log_byte(pspdata[2]);
        debugSerial.println();
      }
      }
    }
    else
      debugSerial.println();

    // Fim da comunicação: Volta ao estado normal
    setLedCommunication(false);
  }

  for (uint8_t i = 3; i < 19; i++)
      tempBuffer[i] = 0x00;

  watchdog_update(); // Adicionado: Alimenta o watchdog
  testLed();
}