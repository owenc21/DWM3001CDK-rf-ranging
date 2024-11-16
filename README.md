# DWM3001CDK RF Ranging Firmware

A firmware for the [Qorvo DWM3001C](https://www.qorvo.com/products/p/DWM3001C) for the purposes of RF Ranging. Forked from [DWM3001C Sample Firmware](https://github.com/Uberi/DWM3001C-starter-firmware)

Currently, this implements firmware for the purpose of building a **connectivity list** in a network of $N$ nodes, where one node is selected as the *initiator* in the network and the remaining $N-1$ nodes are responders.

This repository also has an implementation of firmware for the purposes of building a **connectivity matrix** in a network of $N$ nodes. A connectivity matrix is a distributed data structure in which row $i$ of the matrix corresponds to device $i$'s connectivity list. Essentially it's a distributed record of all devices' own connectivity list in a single data structure.

**Important Note**: Any mention of *flashing* firmware makes use of the DWM3001C's **J9** microUSB port, flashing on the J20 port will not work. Similarly, the device must be powered through the J9 port only.

## Quickstart

```sh
# Build the firmware

make build

# Connect the lower USB port of the DWM3001CDK (labelled J9) to this computer using a USB cable (this is the J-Link's USB port)
# Flash the firmware

make flash

# Save the RTT output to Output/debug-log.txt
make stream-debug-logs

# Stream the RTT output to stdout
make stream-rtt
```

## Requirements
In order to build, flash, and see the output logs, you will need
1. To be on a Linux machine or in a Linux environment (for the Makefile)
2. Have Docker installed

## Connectivity List Firmware
As mentioned, this repository implements firmware for the purposes of building a connectivity list in a network of $N$ nodes.
In such a network, one node is selected as the *initiator*, the node that will build the connnectivity list (a list of distances from itself to the remaining $N-1$ nodes)
The remaining $N-1$ nodes are designated as responders. They serve the purpose of awaiting a *polling message* from the initiator, timestamping the receiving time, and then responding with the timestamp
The initiator calculates the **time-of-flight** (TOF) in this **single-sided ranging** operation. The TOF is used to then estimate the distance to the device.
The initiator cycles through each responder to repeatedly construct the connectivity list in an iterative fashion

### Files
To do this, two files were modified:
1. `Src/examples/ex_06a_ss_twr_initiator/ss_twr_initiator.c`
2. `Src/examples/ex_06b_ss_twr_responder/ss_twr_responder.c`

The `ss_twr_initiator.c` file implements the firmware for the initiator. In this firmware module, the number of responders in the network is specified (`NUM_DEVICES` definition). The connectivity list is constructed and updated in-memory on the device.

To mark a device as an initiator, uncomment the initiator call in `Src/main.c`, run `make build` to build the firmware, then `make flash` to flash the initiator firmware to the device. Ensure you have set the number of responders properly. Then, with the initiator device, you can view the connectivity list being constructed by running `make stream-rtt` to stream the SEGGER Real-Time Terminal (RTT) to the console.

The `ss_twr_responder.c` file implements the firmware for the responder. In this firmware module, the **unique** device ID must be specified (`DEVICE_ID` definition).

The firmware awaits a polling message from the initiator that is specifically marked for it (it will ignore all polling messages not intended for it), make a timestamp, and respond to the initiator.

To mark a device as responder $k$, set the `DEVICE_ID` in the source file appropriately, uncomment the responder function call in `Src/main.c`, then run `make build` to build the firmware, `make flash` to flash the firmware to the device. Now, with any power source, the device will act as a responder.

## Connectivity Matrix Firmware

Expanding off of the previous firmware modules, there is the culmination of all the effort's semester: a firmware to build a distributed connectivity matrix.

In this firmware, each of the $N$ devices are given a hard-coded device id in $\{0,\dots ,N-1\}$. Device $0$ starts as the *initiator* and builds its connectivity list. Then, it adds its list to the connectivity matrix and sends the matrix to device $1$ who repeats the process.

Any node not acting as the initiator is configured to act as a responder. The responder nodes receive messages and first check if the message was intended for their device ID. If so, it then checks if it's a ranging message (in which case it's from an initiator looking to measure distance) or if it's the previous initiator instructing the device to begin initiating.

The file `Src/dist_matrix.c` is the source file for this firmware. Notably, unlike the firmware for building a connectivity list, this only has one module, eliminating the need for separate firmwares depending on role.

To use this firmware, simply set the `DEVICE_ID` definition appropriately and flash all devices with the firmware. In `Src/main.c`, uncomment the call to `dist_matrix()`. The RTT logs will print out the connectivity matrix every $N$ iterations, regardless of which device's logs you look at (hence the point of it being distributed)

### Challenges and Future Steps

Currently we do not employ any form of error-checking. This is an issue, as it is not uncommon to see negative distances appear in the connectivity matrix, likely due to error during ranging or floating point errors.

There is inefficiency in the transmission. Each message transmission sends the connectivity matrix (even if it's unused) as well as a polling message and a response message. Technically, only one of these three are needed for each transmission. The decision was made to include all messages for each transmission because of the ease of implementation.

There is a strict limit on the amount of data that can be sent in a single transmission. To my knowledge, this limit is **127 bytes**. Given the connectivity matrix is a $N\times N$ matrix of `double`s, we cannot currently support more than $N=2$ devices. Obviously, it would be ideal to improve upon this in the future.

**To be clear, the firmware is correct and would work for any arbitrary $N$, but due to limitations on the amount of data transmittable, the firmware will only work for $N\leq 2$. This is not the case with the connectivity list firmware, which sends at most $20$ bytes per transmission**

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

As mentioned, this repository is a fork of [DWM3001C Sample Firmware](https://github.com/Uberi/DWM3001C-starter-firmware). The modified source files originate from that repository. I do not claim any intellectual property as my own with the exception of the modifications made for the purposes described above.
