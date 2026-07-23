#ifndef KEYS_H
#define KEYS_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t byte;

extern const byte answer_08[6];
extern const byte answer_0D[9];
extern const byte answer_16[21];
extern const byte answer_04[6];
extern const byte answer_02[5];
extern const byte answer_09[6];
extern const byte answer_0B[6];
extern const byte answer_07[6];
extern const byte answer_03[6];
extern const byte answer_01[7];
extern const byte answer_90[11];
extern const byte psp_81[4];

extern const byte go_key1[16];
extern const byte go_key2[16];
extern const byte go_secret[16];

bool get_keystore(byte version, byte out_key[16]);
bool get_challenge1_secret(byte version, byte out_secret[8]);
bool get_challenge2_secret(byte version, byte out_secret[8]);

#endif
