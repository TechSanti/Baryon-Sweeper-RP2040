#ifndef KEYS_H
#define KEYS_H

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

extern const byte keystore[][16];
extern const byte challenge1_secret[][8];
extern const byte challenge2_secret[][8];

void get_keystore(byte *buffer, byte key);
void get_challenge1_secret(byte *buffer, byte version);
void get_challenge2_secret(byte *buffer, byte version);

#endif