### About

This folder contains files to integrate JerryScript with Zephyr RTOS to
run on a number of supported boards (like
[Arduino 101 / Genuino 101](https://www.arduino.cc/en/Main/ArduinoBoard101),
[Zephyr Arduino 101](https://www.zephyrproject.org/doc/board/arduino_101.html)).

### How to build

#### 1. Preface

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

Following Zephyr boards were tested: qemu_x86, qemy_cortex_m3, arduino_101.

#### 2. Prepare Zephyr

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

#### 3. Build JerryScript for Zephyr

The easiest way is to build and run on a QEMU emulator:

For x86 architecture:

```
make -f ./build/Makefile.zephyr BOARD=qemu_x86 qemu
```

For ARM (Cortex-M) architecture:

```
make -f ./build/Makefile.zephyr BOARD=qemu_cortex_m3 qemu
```

#### 4. Build for Arduino 101

```
# assume you are in harmony folder
cd jerry
make -f ./targets/zephyr/Makefile.zephyr BOARD=arduino_101_factory
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

#### 5. Flashing

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
make -f ./build/Makefile.zephyr BOARD=arduino_101_factory
```

Follow the Zephyr instructions to flash using the dfu-util command.


- Using JTAG

There is a helper function to flash using the JTAG and Flywatter2

![alt tag](docs/arduino_101.jpg?raw=true "Example")
```
make -f ./build/Makefile.zephyr BOARD=arduino_101_factory flash

```

#### 6. Serial terminal

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
