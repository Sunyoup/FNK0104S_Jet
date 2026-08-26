# FNK0104S_Jet

This is an application of CubeCoders' Jet (https://github.com/CubeCoders/Jet) 3D renderring library.

## Hardware

This is desinged for Freenove's FNK0104S(a.k.a. ES3C40P) board with ESP32-S3 N16R8 & ST7796 SPI display:
https://github.com/Freenove/Freenove_ESP32_S3_Display

## Features

- 3D objects(plane, cube, sphere) are rotating.
- The camera viewpoint can be moved using touch and drag.
- Object magnification can be changed using 2 finger touch.

## BSP from Waveshare

- The resources from Freenove do not have ESP-IDF templates for ST7796 SPI display. So the codes for ESP32-S3-Touch-LCD-3.5 from Waveshare which is also equipped with ST7796 are reused and modified: https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5

## Instruction

This is tested with ESP-IDF v5.5.3 .

```
git clone --recursive https://github.com/Sunyoup/FN0K104S_Jet.git
cd FNK0104S_Jet
rm -rf components/Jet/src/JetConfig.example.hpp
cp JetConfig.hpp components/Jet/src/
idf.py menuconfig
idf.py flash monitor
```
## License

This project is open-source and released under the **GNU General Public License v3.0 (GPL-3.0)** for non-commercial and educational purposes.

### Component Licenses & Attributions

* **Jet 3D Engine**: Created by [CubeCoders](https://cubecoders.com/). 
  * The `components/Jet` directory is subject to the original licensing terms set by CubeCoders.
  * **Commercial Use Notice**: According to CubeCoders' licensing terms, if you plan to use this software or its Jet engine component for commercial purposes, you are required to either release your source code under a compatible open-source license or purchase a commercial license from the original author.

