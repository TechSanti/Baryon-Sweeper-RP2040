Pré-requisitos
Antes de começar, certifique-se de ter o ambiente configurado:

Arduino IDE:

Baixe e instale a versão mais recente do Arduino IDE (2.x ou 1.8.x) em arduino.cc.


Bibliotecas necessárias:

AES: Instale uma biblioteca AES compatível, como "AESLib" ou "TinyAES", via Library Manager no Arduino IDE (Sketch > Include Library > Manage Libraries, pesquise "AES").
Adafruit NeoPixel: Para o RP2040-Zero, instale via Library Manager (pesquise "Adafruit NeoPixel").


Core RP2040:

Adicione o suporte ao RP2040 no Arduino IDE:

Abra o Arduino IDE.
Vá para File > Preferences (ou Arduino IDE > Preferences no macOS).
No campo Additional Boards Manager URLs, adicione:
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json

Clique em OK.
Vá para Tools > Board > Boards Manager, pesquise "RP2040" e instale o Raspberry Pi Pico/RP2040 por Earle F. Philhower.


Hardware:

Raspberry Pi Pico (RP2040 normal) ou Waveshare RP2040-Zero.
Cabo USB para conectar a board ao computador.

Passo a Passo para Compilar em .uf2

Abrir o Projeto no Arduino IDE:

Abra o Arduino IDE.
Vá para File > Open e selecione o arquivo RP2040_v3.2.ino. O keys.h será carregado automaticamente se estiver na mesma pasta.


Configurar a Board:

Vá para Tools > Board > Raspberry Pi Pico/RP2040.
Selecione a board específica:

Para Raspberry Pi Pico: Escolha Raspberry Pi Pico.
Para Waveshare RP2040-Zero: Escolha Waveshare RP2040 Zero.

Verificar o Código:

Clique em Sketch > Verify/Compile (atalho: Ctrl+R) para compilar o código.
O IDE verificará o código e reportará erros, se houver. Certifique-se de que as bibliotecas AES e Adafruit NeoPixel estão instaladas. Se houver erros relacionados à biblioteca AES, tente outra como "TinyAES" ou ajuste o código para uma lib compatível.


Exportar o Arquivo .uf2:

No Arduino IDE, vá para Sketch > Export compiled Binary (atalho: Ctrl+Alt+S no Windows/Linux, ou equivalente no macOS).
Após a compilação, o Arduino IDE gera dois arquivos na pasta do projeto:
