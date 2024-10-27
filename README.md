# DWM3001CDK RF Ranging Firmware

A firmware for the [Qorvo DWM3001C](https://www.qorvo.com/products/p/DWM3001C) for the purposes of RF Ranging. Forked from [DWM3001C Sample Firmware](https://github.com/Uberi/DWM3001C-starter-firmware)

## Quickstart

```sh
# Build the firmware

make build

# Connect the lower USB port of the DWM3001CDK (labelled J9) to this computer using a USB cable (this is the J-Link's USB port)
# Flash the firmware

make flash

# Save the RTT output to Output/debug-log.txt
# Ideally, this will one-day be streamed to the terminal
make stream-debug-logs
```

## Requirements
In order to build, flash, and see the output logs, you will need
1. To be on a Linux machine or in a Linux environment (for the Makefile)
2. Have Docker installed

## License

Most of the code in this repository comes from the official Qorvo SDKs and examples published on their website. Here's the copyright notice that comes with the SDKs:

> Read the header of each file to know more about the license concerning this file.
> Following licenses are used in the SDK:
> 
> * Apache-2.0 license
> * Qorvo license
> * FreeRTOS license
> * Nordic Semiconductor ASA license
> * Garmin Canada license
> * ARM Limited license
> * Third-party licenses: All third-party code contained in SDK_BSP/external (respective licenses included in each of the imported projects)
> 
> The provided HEX files were compiled using the projects located in the folders. For license and copyright information,
> see the individual .c and .h files that are included in the projects.

Therefore, you should carefully read the copyright headers of the individual source files and follow their licenses if you decide to use them. As for the parts I've built, such as the build environment, I release those under the [Creative Commons CC0 license](https://creativecommons.org/public-domain/cc0/) ("No Rights Reserved").
