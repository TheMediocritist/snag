# snag
Copy primary display to a secondary framebuffer for display on a Sharp Memory LCD. Intended for use with [Beeperberry](https://beepberry.sqfmi.com) to enable fast copying of HDMI data to its display.

Forked from https://github.com/AndrewFromMelbourne/raspi2fb with the following changes:
* copies HDMI buffer to Sharp Memory Display buffer
* displays coloured buffer data using Bayer dithering to maintain integrity/accuracy of any black & white data

## Examples

## Usage

    snag <options>

    --daemon             - start in the background as a daemon
    --device <device>    - framebuffer device (default /dev/fb1)
    --display <number>   - Raspberry Pi display number (default 0)
    --fps <fps>          - set desired frames per second (default 10 frames per second)
    --dither <type>      - one of 2x2/4x4/8x8/16x16 (default 2x2)
    --pidfile <pidfile>  - create and lock PID file (if being run as a daemon)
    --once               - copy only one time, then exit
    --help               - print usage and exit

## How to build

1. Install the build tool cmake and the libbsd-dev library
    ```
    sudo apt-get install cmake
    sudo apt-get install libbsd-dev
    ```
2. Build the executable
    ```
    mkdir build
    cd build
    cmake ..
    make
    ```
3. Test to ensure it's running correctly (ctrl+c to quit)
    ```
    ./snag
    ```
4. Install to binaries folder
    ```
    sudo make install
    ```
5. (optional) Run as a system service
    ```
    sudo cp ../raspi2fb@.service /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable raspi2fb@1.service
    sudo systemctl start raspi2fb@1
    ```
### How to uninstall

1. Stop and remove the system service (if setup)
    ```sudo systemctl stop raspi2fb@1
    sudo systemctl disable raspi2fb@1.service
    sudo rm /etc/systemd/system/raspi2fb@.service
    ```
2. Delete the executable file
    ```
    sudo rm /usr/local/bin/raspi2fb
    ```
