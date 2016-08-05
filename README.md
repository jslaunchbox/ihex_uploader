### About

This folder contains files to integrate JerryScript with Zephyr RTOS to
run on a number of supported boards (like
[Arduino 101 / Genuino 101](https://www.arduino.cc/en/Main/ArduinoBoard101),
[Zephyr Arduino 101](https://www.zephyrproject.org/doc/board/arduino_101.html)).

# How to build

## 1. Preface

1. Directory structure

Assume `harmony` as the path to the projects to build.

We have to fetch the different libraries and systems we are going to use.
For that, just run:
```
./scripts/get-dependecies.sh
```

This will fetch everything required into the deps folder

The tree would look something like this.

```
harmony
  + src
  + build
  + scripts
  + deps
     + jerryscript
     + zephyr-project
     + ihex
```

2. Target boards/emulations

Following Zephyr boards were tested: qemu_x86, arduino_101.

## 2. Prepare Zephyr

Follow [this](https://www.zephyrproject.org/doc/getting_started/getting_started.html) page to get
the Zephyr source and configure the environment.

If you just start with Zephyr, you may want to follow "Building a Sample
Application" section in the doc above and check that you can flash your
target board.

Remember to source the Zephyr environment:

```
source ./deps/zephyr-project/zephyr-env.sh

export ZEPHYR_GCC_VARIANT=zephyr

export ZEPHYR_SDK_INSTALL_DIR=<sdk installation directory>
```

## 3. Build JerryScript for Zephyr

The easiest way is to build and run on a QEMU emulator:

For x86 architecture:

```
make -f ./build/Makefile.zephyr BOARD=qemu_x86 qemu
```

## 4. Build for Arduino 101

```
# assume you are in harmony folder
cd jerry
make -f ./targets/zephyr/Makefile.zephyr
```

This will generate the following libraries:
```
./outdir/arduino_101_factory/librelease-cp_minimal.jerry-core.a
./outdir/arduino_101_factory/librelease-cp_minimal.jerry-libm.lib.a
./outdir/arduino_101_factory/librelease.external-cp_minimal-entry.a
```

The final Zephyr image will be located here:
```
./outdir/arduino_101_factory/zephyr/zephyr.elf
```

## 5. Flashing

Details on how to flash the image can be found here:
[Flashing image](https://www.zephyrproject.org/doc/board/arduino_101.html)
(or similar page for other supported boards).

To be able to use this demo in hardware you will need the serial console
which will be generating output to Pins 0 & 1

Some examples of building the software

```
make -f ./build/Makefile.zephyr clean
```

- Not using a Jtag and having a factory stock Arduino 101.

```
make -f ./build/Makefile.zephyr
```

Follow the Zephyr instructions to flash using the dfu-util command.


- Using JTAG

There is a helper function to flash using the JTAG and Flywatter2

![alt tag](docs/arduino_101.jpg?raw=true "Example")
```
make -f ./build/Makefile.zephyr flash

```

## 6. Serial terminal

Test a command line in a serial terminal.


You should see something similar to this:
```
Jerry Compilation May 26 2016 13:37:50
js>
```


Run the example javascript command test function
```
js> test
Script [var test=0; for (t=100; t<1000; t++) test+=t; print ('Hi JS World! '+test);]
Hi JS World! 494550
```


Try more complex functions:
```
js>function hello(t){t=t*10;return t}; print("result"+hello(10.5));
```


Help will provide a list of commands
```
> help
```

This program, is built in top of the Zephyr command line, so there is a limit of 10 spaces.

# 2. USB serial terminal commands

## Setters

### Filename
Set the destination filename where to store the code to run.
```
set filename <filename>
```

### Data transfer
Set the mode to accept data when the transmision starts
1. Raw data 
Will be plain text that contains the code. No CRC or error checking. 
After the transmission is finished you can parse or run the code.

2. Intel Hex 
Basic CRC, hexadecimal data with data sections and regions.
It might be that the code is splitted in sections and you will only update a section of the memory.

3. Snapshot 
Binary format in IntelHex that will be stored on the filename.

```
set transfer ihex
set transfer raw
set transfer snapshot
```

### Read

Starts transaction and save the data to memory or disk

```
read
```

#### Raw
If the device is in RAW mode, it will wait for CTRL+Z or <EOF>
to close the file.
CTRL+X or CTRL+C will cancel the transaction and return to the command line.

#### Ihex
The device will output [BEGIN IHEX]
And will process until end of file :00000001FF

### Getters

```
get filedata <filename>
```

### States

``` 
use filename <filename>
``` 

### System

``` 
bluetooth connect / disconnect / list
``` 

### Execution and flow

Parse the code specified on the 'use' command to check for errors
``` 
parse 
``` 

Run the code specified on the 'use' command
```
run 
``` 

Launches a debug server to step into the jerryscript code
``` 
debug server
``` 

## Data transaction example using IHEX

``` 
set filename launchbox.hex
set transfer ihex
read    
``` 
<Send ihex data here>
``` 
use launchbox.hex
parse
run
``` 