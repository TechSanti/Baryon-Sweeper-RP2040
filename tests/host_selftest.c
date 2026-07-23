#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aes.h"
#include "keys.h"

#ifndef LEGACY_CHALLENGE_MAP
#define LEGACY_CHALLENGE_MAP 1
#endif

#ifndef LEGACY_SERIAL_PACKET
#define LEGACY_SERIAL_PACKET 1
#endif

static uint8_t checksum(const uint8_t *data, size_t length) {
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum = (uint8_t)(sum + data[i]);
    }
    return (uint8_t)(sum ^ 0xFFU);
}

static void assert_precomputed_packet(const uint8_t *packet, size_t length) {
    assert(packet != NULL);
    assert(length >= 2U);
    assert(checksum(packet, length - 1U) == packet[length - 1U]);
}

static void test_precomputed_checksums(void) {
    assert_precomputed_packet(answer_01, sizeof(answer_01));
    assert_precomputed_packet(answer_02, sizeof(answer_02));
    assert_precomputed_packet(answer_03, sizeof(answer_03));
    assert_precomputed_packet(answer_04, sizeof(answer_04));
    assert_precomputed_packet(answer_07, sizeof(answer_07));
    assert_precomputed_packet(answer_08, sizeof(answer_08));
    assert_precomputed_packet(answer_09, sizeof(answer_09));
    assert_precomputed_packet(answer_0B, sizeof(answer_0B));
    assert_precomputed_packet(answer_0D, sizeof(answer_0D));
    assert_precomputed_packet(answer_16, sizeof(answer_16));
    assert_precomputed_packet(psp_81, sizeof(psp_81));

#if LEGACY_SERIAL_PACKET
    const uint8_t serial_legacy[] = {
        0xA5, 0x06, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0x52
    };
    assert(checksum(serial_legacy, sizeof(serial_legacy)) == 0x00U);
#else
    const uint8_t serial_without_checksum[] = {
        0xA5, 0x06, 0x06, 0xFF, 0xFF, 0xFF, 0xFF
    };
    assert(checksum(serial_without_checksum, sizeof(serial_without_checksum)) == 0x52U);
#endif
}

static void test_aes128_ecb_vector(void) {
    const uint8_t key[16] = {
        0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
        0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
    };
    uint8_t block[16] = {
        0x6B, 0xC1, 0xBE, 0xE2, 0x2E, 0x40, 0x9F, 0x96,
        0xE9, 0x3D, 0x7E, 0x11, 0x73, 0x93, 0x17, 0x2A
    };
    const uint8_t expected[16] = {
        0x3A, 0xD7, 0x7B, 0xB4, 0x0D, 0x7A, 0x36, 0x60,
        0xA8, 0x9E, 0xCA, 0xF3, 0x24, 0x66, 0xEF, 0x97
    };

    struct AES_ctx context;
    AES_init_ctx(&context, key);
    AES_ECB_encrypt(&context, block);
    assert(memcmp(block, expected, sizeof(block)) == 0);
}

static void test_version_maps(void) {
    static const uint8_t keystore_versions[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08, 0x09,
        0x0A, 0x0B, 0x0C, 0x0D, 0x2F, 0x97, 0xB3, 0xD9, 0xEB
    };

    uint8_t key[16];
    uint8_t secret[8];

    for (size_t i = 0; i < sizeof(keystore_versions); ++i) {
        assert(get_keystore(keystore_versions[i], key));
    }

    assert(get_keystore(0x09, key));
    assert(key[0] == 0xD2);

#if LEGACY_CHALLENGE_MAP
    /* Compatibilidade byte a byte com o firmware original. */
    assert(get_challenge1_secret(0x09, secret));
    assert(secret[0] == 0xC2 && secret[7] == 0x5F);

    assert(get_challenge1_secret(0x0A, secret));
    assert(secret[0] == 0x58 && secret[7] == 0x62);

    assert(get_challenge2_secret(0x0D, secret));
    assert(secret[0] == 0xE3 && secret[7] == 0x98);

    assert(get_challenge1_secret(0xB3, secret));
    for (size_t i = 0; i < sizeof(secret); ++i) {
        assert(secret[i] == 0U);
    }
#else
    static const uint8_t challenge_versions[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x08,
        0x0A, 0x0D, 0x2F, 0x97, 0xB3, 0xD9, 0xEB
    };

    for (size_t i = 0; i < sizeof(challenge_versions); ++i) {
        assert(get_challenge1_secret(challenge_versions[i], secret));
        assert(get_challenge2_secret(challenge_versions[i], secret));
    }

    assert(!get_challenge1_secret(0x09, secret));
    assert(!get_challenge1_secret(0x0B, secret));
    assert(!get_challenge1_secret(0x0C, secret));

    assert(get_challenge1_secret(0x0A, secret));
    assert(secret[0] == 0xC2 && secret[7] == 0x5F);

    assert(get_challenge1_secret(0xEB, secret));
    assert(secret[0] == 0x0B && secret[7] == 0x23);
#endif
}

int main(void) {
    test_precomputed_checksums();
    test_aes128_ecb_vector();
    test_version_maps();
    puts("Todos os testes locais passaram.");
    return 0;
}
