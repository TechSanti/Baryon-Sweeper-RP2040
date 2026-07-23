#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/uart.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "aes.h"
#include "keys.h"

#ifdef USE_NEOPIXEL
#include "ws2812.h"
#endif

#ifndef DEBUG_LOG
#define DEBUG_LOG 0
#endif

#ifndef LED_PIN
#define LED_PIN 25
#endif

#ifndef NEOPIXEL_PIN
#define NEOPIXEL_PIN 16
#endif

#ifndef STRICT_RX_CHECKSUM
#define STRICT_RX_CHECKSUM 0
#endif

#ifndef LEGACY_SERIAL_PACKET
#define LEGACY_SERIAL_PACKET 1
#endif

#define UART_ID uart0
#define BAUD_RATE 19200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_EVEN
#define TX_PIN 0
#define RX_PIN 1

#define PSP_REQUEST_SOF 0x5A
#define SYSCON_RESPONSE_SOF 0xA5
#define MSG_MAX_LENGTH 64U
#define UART_BYTE_TIMEOUT_MS 120U
#define GO_REPLY_TIMEOUT_MS 300U
#define AUTH_SESSION_TIMEOUT_US 5000000ULL

#define LOG(...)                   \
    do {                           \
        if (DEBUG_LOG) {           \
            printf(__VA_ARGS__);   \
        }                          \
    } while (0)

typedef enum {
    CMD_INIT = 0x01,
    CMD_READ_CODE = 0x02,
    CMD_READ_DATA_03 = 0x03,
    CMD_READ_DATA_04 = 0x04,
    CMD_READ_DATA_07 = 0x07,
    CMD_READ_DATA_08 = 0x08,
    CMD_READ_DATA_09 = 0x09,
    CMD_READ_DATA_0B = 0x0B,
    CMD_READ_SERIAL = 0x0C,
    CMD_READ_DATA_0D = 0x0D,
    CMD_READ_DATA_16 = 0x16,
    CMD_SYSCON_CHALLENGE = 0x80,
    CMD_SYSCON_RESPONSE = 0x81,
    CMD_PSP_GO_AUTH = 0x90
} SysconCommand;

typedef enum {
    AUTH_IDLE,
    AUTH_CHALLENGE_READY,
    AUTH_GO_READY
} AuthState;

typedef enum {
    FRAME_OK,
    FRAME_TIMEOUT,
    FRAME_BAD_SOF,
    FRAME_BAD_LENGTH,
    FRAME_BAD_CHECKSUM
} FrameResult;

typedef struct {
    uint8_t command;
    uint8_t payload[MSG_MAX_LENGTH];
    size_t payload_length;
    bool checksum_valid;
} RequestFrame;

#if LEGACY_SERIAL_PACKET
/* Mantém exatamente o pacote usado pelo firmware original, incluindo o 0x52
 * já armazenado e o checksum adicional 0x00 gerado no envio. */
static const uint8_t serial_code[] = {
    0xA5, 0x06, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0x52
};
#else
static const uint8_t serial_code[] = {
    0xA5, 0x06, 0x06, 0xFF, 0xFF, 0xFF, 0xFF
};
#endif

static AuthState auth_state = AUTH_IDLE;
static uint8_t auth_version;
static uint8_t challenge1b[16];
static uint64_t auth_started_us;
static uint64_t led_time_us;
static bool led_on;

static void log_bytes(const char *prefix, const uint8_t *data, size_t length) {
    if (!DEBUG_LOG) {
        return;
    }

    printf("%s", prefix);
    for (size_t i = 0; i < length; ++i) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

static uint8_t packet_checksum(const uint8_t *data, size_t length) {
    uint8_t sum = 0;

    for (size_t i = 0; i < length; ++i) {
        sum = (uint8_t)(sum + data[i]);
    }

    return (uint8_t)(sum ^ 0xFFU);
}

static bool uart_read_byte_timeout(uint8_t *out, uint32_t timeout_ms) {
    if (out == NULL) {
        return false;
    }

    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    while (!uart_is_readable(UART_ID)) {
        watchdog_update();

        if (time_reached(deadline)) {
            return false;
        }

        tight_loop_contents();
    }

    *out = (uint8_t)uart_getc(UART_ID);
    return true;
}

static bool uart_read_exact(uint8_t *buffer, size_t length, uint32_t timeout_ms) {
    if (buffer == NULL && length != 0U) {
        return false;
    }

    for (size_t i = 0; i < length; ++i) {
        if (!uart_read_byte_timeout(&buffer[i], timeout_ms)) {
            return false;
        }
    }

    return true;
}

static FrameResult read_request_frame(RequestFrame *frame) {
    uint8_t header[3];
    uint8_t received_checksum;

    if (frame == NULL) {
        return FRAME_BAD_LENGTH;
    }

    memset(frame, 0, sizeof(*frame));

    /*
     * Comunicação de uma via normalmente devolve eco dos bytes transmitidos.
     * As respostas começam em 0xA5, então elas são ignoradas até o próximo
     * pedido real do PSP, que começa em 0x5A. Não existe limite de 16 bytes,
     * pois uma resposta de challenge possui 20 bytes e pode ser ecoada inteira.
     */
    for (;;) {
        if (!uart_read_byte_timeout(&header[0], UART_BYTE_TIMEOUT_MS)) {
            return FRAME_TIMEOUT;
        }
        if (header[0] == PSP_REQUEST_SOF) {
            break;
        }
    }

    if (!uart_read_exact(&header[1], 2U, UART_BYTE_TIMEOUT_MS)) {
        return FRAME_TIMEOUT;
    }

    if (header[1] < 2U) {
        return FRAME_BAD_LENGTH;
    }

    frame->command = header[2];
    frame->payload_length = (size_t)header[1] - 2U;

    if (frame->payload_length > sizeof(frame->payload)) {
        return FRAME_BAD_LENGTH;
    }

    if (!uart_read_exact(frame->payload, frame->payload_length, UART_BYTE_TIMEOUT_MS) ||
        !uart_read_byte_timeout(&received_checksum, UART_BYTE_TIMEOUT_MS)) {
        return FRAME_TIMEOUT;
    }

    uint8_t sum = 0;
    for (size_t i = 0; i < sizeof(header); ++i) {
        sum = (uint8_t)(sum + header[i]);
    }
    for (size_t i = 0; i < frame->payload_length; ++i) {
        sum = (uint8_t)(sum + frame->payload[i]);
    }

    frame->checksum_valid = ((uint8_t)(sum ^ 0xFFU) == received_checksum);

    if (DEBUG_LOG) {
        LOG("PSP: %02X %02X %02X ", header[0], header[1], header[2]);
        for (size_t i = 0; i < frame->payload_length; ++i) {
            LOG("%02X ", frame->payload[i]);
        }
        LOG("%02X%s\n", received_checksum,
            frame->checksum_valid ? "" : " [checksum diferente]");
    }

#if STRICT_RX_CHECKSUM
    if (!frame->checksum_valid) {
        return FRAME_BAD_CHECKSUM;
    }
#endif

    return FRAME_OK;
}

static void write_raw_packet(const uint8_t *packet, size_t length) {
    if (packet == NULL || length == 0U) {
        return;
    }

    uart_write_blocking(UART_ID, packet, length);
    uart_tx_wait_blocking(UART_ID);
    log_bytes("TX:  ", packet, length);
}

static void write_packet_with_checksum(const uint8_t *packet_without_checksum,
                                       size_t length) {
    if (packet_without_checksum == NULL || length == 0U) {
        return;
    }

    const uint8_t checksum = packet_checksum(packet_without_checksum, length);
    uart_write_blocking(UART_ID, packet_without_checksum, length);
    uart_write_blocking(UART_ID, &checksum, 1U);
    uart_tx_wait_blocking(UART_ID);

    if (DEBUG_LOG) {
        log_bytes("TX:  ", packet_without_checksum, length);
        LOG("TX checksum: %02X\n", checksum);
    }
}

static void set_led_communication(bool communicating) {
#ifdef USE_NEOPIXEL
    if (communicating) {
        ws2812_put_pixel(0xFF, 0x00, 0x00);
    } else if (led_on) {
        ws2812_put_pixel(0x00, 0xFF, 0x00);
    } else {
        ws2812_off();
    }
#else
    gpio_put(LED_PIN, communicating || led_on);
#endif
}

static void update_idle_led(void) {
    if (serial_code[3] != 0xFFU) {
        led_on = false;
        set_led_communication(false);
        return;
    }

    const uint64_t now = time_us_64();
    if ((now - led_time_us) >= 1000000ULL) {
        led_time_us = now;
        led_on = !led_on;
        set_led_communication(false);
    }
}

static void reset_authentication(void) {
    auth_state = AUTH_IDLE;
    auth_version = 0;
    auth_started_us = 0;
    memset(challenge1b, 0, sizeof(challenge1b));
}

static bool auth_session_expired(void) {
    return auth_state != AUTH_IDLE &&
           (time_us_64() - auth_started_us) > AUTH_SESSION_TIMEOUT_US;
}

static void matrix_swap(uint8_t data[16]) {
    static const uint8_t map[16] = {
        0x00, 0x04, 0x08, 0x0C,
        0x01, 0x05, 0x09, 0x0D,
        0x02, 0x06, 0x0A, 0x0E,
        0x03, 0x07, 0x0B, 0x0F
    };
    uint8_t temporary[16];

    for (size_t i = 0; i < sizeof(temporary); ++i) {
        temporary[i] = data[map[i]];
    }

    memcpy(data, temporary, sizeof(temporary));
}

static void aes_ecb_encrypt_block(const uint8_t plain[16],
                                  uint8_t cipher[16],
                                  const uint8_t key[16]) {
    struct AES_ctx context;

    AES_init_ctx(&context, key);
    memcpy(cipher, plain, AES_BLOCKLEN);
    AES_ECB_encrypt(&context, cipher);
}

static bool aes_cbc_decrypt_zero_iv(const uint8_t *cipher,
                                    size_t size,
                                    uint8_t *plain,
                                    const uint8_t key[16]) {
    if (cipher == NULL || plain == NULL || key == NULL || size == 0U ||
        (size % AES_BLOCKLEN) != 0U) {
        return false;
    }

    struct AES_ctx context;
    uint8_t iv[AES_BLOCKLEN] = {0};

    AES_init_ctx_iv(&context, key, iv);
    memcpy(plain, cipher, size);
    AES_CBC_decrypt_buffer(&context, plain, size);
    return true;
}

static bool constant_time_equal(const uint8_t *left,
                                const uint8_t *right,
                                size_t length) {
    if (left == NULL || right == NULL) {
        return false;
    }

    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) {
        difference |= (uint8_t)(left[i] ^ right[i]);
    }

    return difference == 0U;
}

static bool mix_challenge1(uint8_t data[16],
                           uint8_t version,
                           const uint8_t challenge[8]) {
    uint8_t secret[8];

    if (!get_challenge1_secret(version, secret)) {
        return false;
    }

    data[0] = secret[0];
    data[4] = secret[1];
    data[8] = secret[2];
    data[12] = secret[3];
    data[1] = secret[4];
    data[5] = secret[5];
    data[9] = secret[6];
    data[13] = secret[7];
    data[2] = challenge[0];
    data[6] = challenge[1];
    data[10] = challenge[2];
    data[14] = challenge[3];
    data[3] = challenge[4];
    data[7] = challenge[5];
    data[11] = challenge[6];
    data[15] = challenge[7];
    return true;
}

static bool mix_challenge2(uint8_t data[16],
                           uint8_t version,
                           const uint8_t challenge[16]) {
    uint8_t secret[8];

    if (!get_challenge2_secret(version, secret)) {
        return false;
    }

    data[0] = challenge[0];
    data[4] = challenge[1];
    data[8] = challenge[2];
    data[12] = challenge[3];
    data[1] = challenge[4];
    data[5] = challenge[5];
    data[9] = challenge[6];
    data[13] = challenge[7];
    data[2] = secret[0];
    data[6] = secret[1];
    data[10] = secret[2];
    data[14] = secret[3];
    data[3] = secret[4];
    data[7] = secret[5];
    data[11] = secret[6];
    data[15] = secret[7];
    return true;
}

static bool build_challenge_response(const RequestFrame *frame,
                                     uint8_t response[19]) {
    uint8_t key[16];
    uint8_t mixed[16] = {0};
    uint8_t challenge1a[16];
    uint8_t second[16];

    if (frame == NULL || response == NULL || frame->payload_length < 9U) {
        return false;
    }

    const uint8_t version = frame->payload[0];

    if (!get_keystore(version, key)) {
        LOG("Keystore não encontrado para versão %02X; enviando placeholders\n", version);
        response[0] = SYSCON_RESPONSE_SOF;
        response[1] = 0x12;
        response[2] = 0x06;
        memset(&response[3], 0xFF, 16U);
        auth_version = version;
        auth_state = AUTH_CHALLENGE_READY;
        auth_started_us = time_us_64();
        memset(challenge1b, 0, sizeof(challenge1b));
        return true;
    }

    if (!mix_challenge1(mixed, version, &frame->payload[1])) {
        LOG("Secret de challenge ausente para versão %02X; enviando placeholders\n", version);
        response[0] = SYSCON_RESPONSE_SOF;
        response[1] = 0x12;
        response[2] = 0x06;
        memset(&response[3], 0xFF, 16U);
        auth_version = version;
        auth_state = AUTH_CHALLENGE_READY;
        auth_started_us = time_us_64();
        memset(challenge1b, 0, sizeof(challenge1b));
        return true;
    }

    matrix_swap(mixed);
    aes_ecb_encrypt_block(mixed, challenge1a, key);
    memcpy(second, challenge1a, sizeof(second));
    aes_ecb_encrypt_block(second, challenge1b, key);
    matrix_swap(challenge1b);

    response[0] = SYSCON_RESPONSE_SOF;
    response[1] = 0x12;
    response[2] = 0x06;
    memcpy(&response[3], challenge1a, 8U);
    memcpy(&response[11], challenge1b, 8U);

    auth_version = version;
    auth_state = AUTH_CHALLENGE_READY;
    auth_started_us = time_us_64();
    return true;
}

static bool build_syscon_response2(uint8_t response[19]) {
    uint8_t key[16];
    uint8_t mixed[16] = {0};
    uint8_t challenge2[16];
    uint8_t response2[16];

    if (response == NULL || auth_state == AUTH_IDLE) {
        return false;
    }

    if (!get_keystore(auth_version, key) ||
        !mix_challenge2(mixed, auth_version, challenge1b)) {
        return false;
    }

    matrix_swap(mixed);
    aes_ecb_encrypt_block(mixed, challenge2, key);
    aes_ecb_encrypt_block(challenge2, response2, key);

    response[0] = SYSCON_RESPONSE_SOF;
    response[1] = 0x12;
    response[2] = 0x06;
    memcpy(&response[3], response2, sizeof(response2));
    return true;
}

static bool perform_go_transition(void) {
    uint8_t reply[4];

    write_raw_packet(psp_81, sizeof(psp_81));

    if (!uart_read_exact(reply, sizeof(reply), GO_REPLY_TIMEOUT_MS)) {
        LOG("Timeout na transição do PSP Go\n");
        return false;
    }

    /* O firmware original apenas consumia quatro bytes e prosseguia. Em uma
     * ligação de um fio, esses quatro bytes podem inclusive ser o próprio eco. */
    log_bytes("Resposta/eco GO: ", reply, sizeof(reply));
    write_raw_packet(answer_01, sizeof(answer_01));
    return true;
}

static bool build_go_auth_response(const RequestFrame *frame,
                                   uint8_t response[43]) {
    uint8_t encrypted_request[32];
    uint8_t payload[32];
    uint8_t payload91[32] = {0};
    uint8_t encrypted_response[32];

    if (frame == NULL || response == NULL || frame->payload_length < 40U) {
        return false;
    }

    memcpy(encrypted_request, &frame->payload[8], sizeof(encrypted_request));

    if (!aes_cbc_decrypt_zero_iv(encrypted_request,
                                 sizeof(encrypted_request),
                                 payload,
                                 go_key1)) {
        return false;
    }

    if (!constant_time_equal(&payload[16], go_secret, sizeof(go_secret))) {
        LOG("Solicitação de autenticação PSP Go inválida\n");
        return false;
    }

    memcpy(&payload91[0], &payload[8], 8U);
    memcpy(&payload91[8], &payload[0], 8U);

    /* Mantido igual ao protocolo original: CBC decrypt com IV zero. */
    if (!aes_cbc_decrypt_zero_iv(payload91,
                                 sizeof(payload91),
                                 encrypted_response,
                                 go_key2)) {
        return false;
    }

    memcpy(response, answer_90, sizeof(answer_90));
    memcpy(&response[sizeof(answer_90)], encrypted_response, sizeof(encrypted_response));
    return true;
}

static void handle_request(const RequestFrame *frame) {
    uint8_t dynamic_response[43];

    if (frame == NULL) {
        return;
    }

    if (auth_session_expired()) {
        LOG("Sessão de autenticação expirada\n");
        reset_authentication();
    }

    switch ((SysconCommand)frame->command) {
        case CMD_INIT:
            write_raw_packet(answer_01, sizeof(answer_01));
            break;

        case CMD_READ_SERIAL:
            write_packet_with_checksum(serial_code, sizeof(serial_code));
            break;

        case CMD_SYSCON_CHALLENGE:
            reset_authentication();
            if (build_challenge_response(frame, dynamic_response)) {
                write_packet_with_checksum(dynamic_response, 19U);
            } else {
                reset_authentication();
            }
            break;

        case CMD_SYSCON_RESPONSE:
            if (build_syscon_response2(dynamic_response)) {
                write_packet_with_checksum(dynamic_response, 19U);

                if (auth_version == 0xEBU || auth_version == 0xB3U) {
                    if (perform_go_transition()) {
                        auth_state = AUTH_GO_READY;
                        auth_started_us = time_us_64();
                    } else {
                        reset_authentication();
                    }
                } else {
                    reset_authentication();
                }
            } else {
                LOG("CMD 0x81 sem challenge válido disponível\n");
            }
            break;

        case CMD_PSP_GO_AUTH:
            if (build_go_auth_response(frame, dynamic_response)) {
                write_packet_with_checksum(dynamic_response, sizeof(dynamic_response));
            } else {
                LOG("CMD 0x90 inválido ou fora da sequência\n");
            }
            reset_authentication();
            break;

        case CMD_READ_DATA_03:
            write_raw_packet(answer_03, sizeof(answer_03));
            break;
        case CMD_READ_DATA_07:
            write_raw_packet(answer_07, sizeof(answer_07));
            break;
        case CMD_READ_DATA_0B:
            write_raw_packet(answer_0B, sizeof(answer_0B));
            break;
        case CMD_READ_DATA_09:
            write_raw_packet(answer_09, sizeof(answer_09));
            break;
        case CMD_READ_CODE:
            write_raw_packet(answer_02, sizeof(answer_02));
            break;
        case CMD_READ_DATA_04:
            write_raw_packet(answer_04, sizeof(answer_04));
            break;
        case CMD_READ_DATA_16:
            write_raw_packet(answer_16, sizeof(answer_16));
            break;
        case CMD_READ_DATA_0D:
            write_raw_packet(answer_0D, sizeof(answer_0D));
            break;
        case CMD_READ_DATA_08:
            write_raw_packet(answer_08, sizeof(answer_08));
            break;

        default:
            LOG("Comando não suportado: %02X\n", frame->command);
            break;
    }
}

static void initialize_led(void) {
#ifdef USE_NEOPIXEL
    if (!ws2812_init(NEOPIXEL_PIN)) {
        LOG("Não foi possível reservar uma state machine PIO para o WS2812\n");
    }
    ws2812_put_pixel(0x00, 0xFF, 0x00);
#else
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);
#endif
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

    initialize_led();
    reset_authentication();

    LOG("Baryon Sweeper iniciado\n");
    watchdog_enable(3000U, true);

    for (;;) {
        watchdog_update();

        if (uart_is_readable(UART_ID)) {
            RequestFrame frame;
            set_led_communication(true);

            const FrameResult result = read_request_frame(&frame);
            if (result == FRAME_OK) {
                handle_request(&frame);
            } else {
                LOG("Quadro UART ignorado, erro=%d\n", (int)result);
                /* Não apaga o estado: o erro pode ser somente eco da própria
                 * resposta ou um ACK do PSP entre 0x80 e 0x81. */
            }

            set_led_communication(false);
        }

        update_idle_led();
        tight_loop_contents();
    }
}
