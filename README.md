# **RP2040-ZERO**
<img width="2051" height="1556" alt="Image" src="https://github.com/user-attachments/assets/c1d39e4a-35a8-4645-8264-9f7b82a95306" />


# **RP2040-PI PICO**
<img width="2548" height="1556" alt="Image" src="https://github.com/user-attachments/assets/d1dea699-5834-4944-8c52-c3e9547615c0" />


# **PSP GO**
<img width="4096" height="1024" alt="Image" src="https://github.com/user-attachments/assets/611ab0e6-a81c-4a3a-9ac3-c05a68344d4c" />


# **PSP STREET**
![Image](https://github.com/user-attachments/assets/3b96cbb8-b907-4dba-80b1-60c8b0393d93)


# compilar
**RP 2040 Pi Pico**

mkdir build

cd build

cmake -G Ninja ..

ninja


**RP 2040 Zero**

mkdir build_zero

cd build_zero

cmake -G Ninja -DUSE_RP2040_ZERO=ON ..

ninja
