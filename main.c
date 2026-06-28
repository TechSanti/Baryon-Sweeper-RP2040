#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/time.h"
#include "aes.h"
#include "keys.h"

// UART para o PSP (19200 8E1)
#define UART_ID       uart0
#define BAUD_RATE     19200
#define DATA_BITS     8
#define STOP_BITS     1
#define PARITY        UART_PARITY_EVEN

#define TX_PIN        0
#define RX_PIN        1

// LED padrão (GPIO25) se não estiver definido USE_NEOPIXEL
#ifndef USE_NEOPIXEL
#define LED_PIN       25
#endif

// Buffers globais
static uint8_t serial_code[8] = {0};
static uint8_t msg[64] = {0};
static uint8_t msgLength = 0;
static uint8_t version = 0;
static uint8_t pspdata[4] = {0};
static uint8_t challenge1a[16] = {0};
static uint8_t challenge1b[16] = {0};
static uint8_t tempBuffer[64] = {0};

static uint64_t ledTime = 0;
static bool ledOn = false;

// Funções auxiliares
static void log_byte(uint8_t b) {
    printf("%02X ", b);
}

static uint8_t GeneratePacketChecksum(uint8_t *inputArray, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++)
        sum += inputArray[i];
    return ((sum & 0xFF) ^ 0xFF);
}

static void psp_write(uint8_t *b, uint8_t len, bool generateChecksum) {
    uint8_t tempCS[1] = {0};
    uart_write_blocking(UART_ID, b, len);
    if (generateChecksum) {
        tempCS[0] = GeneratePacketChecksum(b, len);
        uart_write_blocking(UART_ID, tempCS, 1);
    }

    printf("Arduino: ");
    for (uint8_t i = 0; i < len; i++)
        log_byte(b[i]);
    if (generateChecksum)
        log_byte(tempCS[0]);
    printf("\n");
}

static void MatrixSwap(uint8_t* data, uint8_t length) {
    static const uint8_t newmap[16] = {
        0x00, 0x04, 0x08, 0x0C,
        0x01, 0x05, 0x09, 0x0D,
        0x02, 0x06, 0x0A, 0x0E,
        0x03, 0x07, 0x0B, 0x0F
    };
    uint8_t temp[16];
    for (uint8_t i = 0; i < length; i++)
        temp[i] = data[newmap[i]];
    memcpy(data, temp, length);
}

static void setLedCommunication(bool isCommunicating) {
#ifdef USE_NEOPIXEL
    if (isCommunicating) {
        ws2812_put_pixel(0xFF, 0x00, 0x00); // vermelho
    } else {
        if (ledOn)
            ws2812_put_pixel(0x00, 0x00, 0xFF); // azul
        else
            ws2812_put_pixel(0x00, 0x00, 0x00);
    }
#else
    gpio_put(LED_PIN, isCommunicating ? 1 : (ledOn ? 1 : 0));
#endif
}

static void testLed(void) {
    if (serial_code[3] == 0xFF) {
        if (time_us_64() - ledTime > 1000000) {
            ledTime = time_us_64();
            ledOn = !ledOn;
            setLedCommunication(false);
        }
    } else {
#ifdef USE_NEOPIXEL
        ws2812_put_pixel(0, 0, 0);
#else
        gpio_put(LED_PIN, 0);
#endif
    }
}

// AES ECB usando tiny-AES-c
static void ecb_encrypt(uint8_t* plain, uint8_t* cipher, uint8_t* key, int keySize) {
    (void)keySize;
    struct AES_ctx ctx;
    AES_init_ctx(&ctx, key);
    memcpy(cipher, plain, 16);
    AES_ECB_encrypt(&ctx, cipher);
}

// AES CBC com IV zero
static void do_aes_decrypt(uint8_t* cipher, int size, uint8_t* plain, uint8_t* key) {
    struct AES_ctx ctx;
    uint8_t iv[16] = {0};
    AES_init_ctx_iv(&ctx, key, iv);
    memcpy(plain, cipher, size);
    AES_CBC_decrypt_buffer(&ctx, plain, size);
}

static void MixChallenge1(uint8_t *data, uint8_t ver, uint8_t *challenge) {
    uint8_t secret1[8];
    get_challenge1_secret(secret1, ver);

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

static void MixChallenge2(uint8_t *data, uint8_t ver, uint8_t *challenge) {
    uint8_t secret2[8];
    get_challenge2_secret(secret2, ver);

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

static void generateSysconResponses(void) {
    version = msg[0];
    uint8_t tempKey[16] = {0};
    uint8_t req[msgLength - 1];
    uint8_t data[16];
    uint8_t second[16];

    get_keystore(tempKey, version);

    tempBuffer[0] = 0xA5;
    tempBuffer[1] = 0x12;
    tempBuffer[2] = 0x06;

    if (tempKey[0] == 0) {
        printf("Key not found\n");
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

int main(void) {
    stdio_init_all();

    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(RX_PIN, GPIO_FUNC_UART);
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(UART_ID, false);

    gpio_pull_up(RX_PIN);
    gpio_pull_up(TX_PIN);

#ifdef USE_NEOPIXEL
    ws2812_init(16);
    ws2812_put_pixel(0, 0, 255);
#else
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);
#endif

    serial_code[0] = 0xA5;
    serial_code[1] = 0x06;
    serial_code[2] = 0x06;
    serial_code[3] = 0xFF;
    serial_code[4] = 0xFF;
    serial_code[5] = 0xFF;
    serial_code[6] = 0xFF;
    serial_code[7] = 0x52;

    printf("Start service\n");
    watchdog_enable(2000, 1);

    while (1) {
        if (uart_is_readable(UART_ID)) {
            setLedCommunication(true);

            pspdata[0] = uart_getc(UART_ID);
            printf("PSP:     ");
            log_byte(pspdata[0]);

            if (pspdata[0] == 0x5A) {
                pspdata[1] = uart_getc(UART_ID);
                pspdata[2] = uart_getc(UART_ID);
                log_byte(pspdata[1]);
                log_byte(pspdata[2]);

                if (pspdata[1] == 0x02) {
                    msgLength = 0;
                } else {
                    msgLength = ((uint8_t)pspdata[1] - 2);

                    absolute_time_t timeout = make_timeout_time_ms(100);
                    while (!uart_is_readable(UART_ID) && !time_reached(timeout))
                        tight_loop_contents();

                    if (time_reached(timeout)) {
                        printf("Timeout waiting for message bytes\n");
                        setLedCommunication(false);
                        continue;
                    }

                    for (uint8_t i = 0; i < msgLength; i++)
                        msg[i] = uart_getc(UART_ID);

                    printf("( ");
                    for (uint8_t i = 0; i < msgLength; i++)
                        log_byte(msg[i]);
                    printf(") ");
                }

                pspdata[3] = uart_getc(UART_ID);
                log_byte(pspdata[3]);
                printf("\n");

                switch (pspdata[2]) {
                case 0x01:
                    memcpy(tempBuffer, answer_01, sizeof(answer_01));
                    psp_write(tempBuffer, sizeof(answer_01), false);
                    break;
                case 0x0C:
                    psp_write(serial_code, 8, true);
                    break;
                case 0x80:
                    generateSysconResponses();
                    psp_write(tempBuffer, 19, true);
                    break;
                case 0x81:
                    tempBuffer[0] = 0xA5;
                    tempBuffer[1] = 0x12;
                    tempBuffer[2] = 0x06;
                    {
                        uint8_t data2[16] = {0};
                        uint8_t tempKey[16];
                        uint8_t challenge2[16];
                        uint8_t response2[16];

                        MixChallenge2(data2, version, challenge1b);
                        MatrixSwap(data2, 16);
                        get_keystore(tempKey, version);
                        ecb_encrypt(data2, challenge2, tempKey, 16);
                        ecb_encrypt(challenge2, response2, tempKey, 16);
                        memcpy(tempBuffer + 3, response2, 16);
                    }
                    psp_write(tempBuffer, 19, true);

                    if (version == 0xEB || version == 0xB3) {
                        const uint8_t psp_81[] = {0x5A, 0x02, 0x01, 0xA2};
                        uart_write_blocking(UART_ID, psp_81, 4);
                        printf("GO:      ");
                        pspdata[0] = uart_getc(UART_ID);
                        pspdata[1] = uart_getc(UART_ID);
                        pspdata[2] = uart_getc(UART_ID);
                        pspdata[3] = uart_getc(UART_ID);
                        memcpy(tempBuffer, answer_01, sizeof(answer_01));
                        psp_write(tempBuffer, sizeof(answer_01), false);
                    }
                    break;
                case 0x90:
                    {
                        uint8_t req[32], tempKey[16], payload[32], payload91[32];
                        uint8_t temp_go_secret[16], resp2[32];
                        memcpy(req, msg + 8, 32);
                        memcpy(tempKey, go_key1, 16);
                        do_aes_decrypt(req, 32, payload, tempKey);
                        memcpy(payload91, payload + 8, 8);
                        memcpy(payload91 + 8, payload, 8);
                        memset(payload91 + 16, 0, 16);
                        memcpy(temp_go_secret, go_secret, 16);
                        bool valid = true;
                        for (int i = 0; i < 16; i++)
                            if (payload[16 + i] != temp_go_secret[i]) {
                                valid = false;
                                break;
                            }
                        if (!valid) {
                            printf("Invalid request from Syscon\n");
                            break;
                        }
                        printf("Go Handshake Request is valid\n");
                        memcpy(tempKey, go_key2, 16);
                        do_aes_decrypt(payload91, 32, resp2, tempKey);
                        memcpy(tempBuffer, answer_90, sizeof(answer_90));
                        memcpy(tempBuffer + sizeof(answer_90), resp2, 32);
                        psp_write(tempBuffer, sizeof(answer_90) + 32, true);
                    }
                    break;
                case 0x03:
                    memcpy(tempBuffer, answer_03, sizeof(answer_03));
                    psp_write(tempBuffer, sizeof(answer_03), false);
                    break;
                case 0x07:
                    memcpy(tempBuffer, answer_07, sizeof(answer_07));
                    psp_write(tempBuffer, sizeof(answer_07), false);
                    break;
                case 0x0B:
                    memcpy(tempBuffer, answer_0B, sizeof(answer_0B));
                    psp_write(tempBuffer, sizeof(answer_0B), false);
                    break;
                case 0x09:
                    memcpy(tempBuffer, answer_09, sizeof(answer_09));
                    psp_write(tempBuffer, sizeof(answer_09), false);
                    break;
                case 0x02:
                    memcpy(tempBuffer, answer_02, sizeof(answer_02));
                    psp_write(tempBuffer, sizeof(answer_02), false);
                    break;
                case 0x04:
                    memcpy(tempBuffer, answer_04, sizeof(answer_04));
                    psp_write(tempBuffer, sizeof(answer_04), false);
                    break;
                case 0x16:
                    memcpy(tempBuffer, answer_16, sizeof(answer_16));
                    psp_write(tempBuffer, sizeof(answer_16), false);
                    break;
                case 0x0D:
                    memcpy(tempBuffer, answer_0D, sizeof(answer_0D));
                    psp_write(tempBuffer, sizeof(answer_0D), false);
                    break;
                case 0x08:
                    memcpy(tempBuffer, answer_08, sizeof(answer_08));
                    psp_write(tempBuffer, sizeof(answer_08), false);
                    break;
                default:
                    printf("No option selected %02X\n", pspdata[2]);
                }
            } else {
                printf("\n");
            }
            setLedCommunication(false);
        }

        for (uint8_t i = 3; i < 19; i++)
            tempBuffer[i] = 0x00;

        watchdog_update();
        testLed();
    }

    return 0;
}