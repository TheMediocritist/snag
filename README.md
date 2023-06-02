# snag
Copy primary display to a secondary framebuffer for display on a Sharp Memory LCD. Intended for use with [Beeperberry](https://beepberry.sqfmi.com) to enable fast copying of HDMI data to its display.

Forked from https://github.com/AndrewFromMelbourne/raspi2fb with the following changes:
* copies 16-bit HDMI buffer to 8-bit Sharp display buffer
* displays coloured buffer data using Bayer dithering to maintain integrity/accuracy of any black & white data

# usage

    snag <options>

    --daemon - start in the background as a daemon
    --device <device> - framebuffer device (default /dev/fb1)
    --display <number> - Raspberry Pi display number (default 0)
    --fps <fps> - set desired frames per second (default 10 frames per second)
    --dithertype <type> - one of 2x2/4x4/8x8/16x16 (default 4x4)
    --pidfile <pidfile> - create and lock PID file (if being run as a daemon)
    --once - copy only one time, then exit
    --help - print usage and exit

# build prerequisites
## cmake
You will need to install cmake

    sudo apt-get install cmake
## libraries
You will need to install libbsd-dev

    sudo apt-get install libbsd-dev
# build
    mkdir build
    cd build
    cmake ..
    make

# install
    sudo make install
    sudo cp ../raspi2fb@.service /etc/systemd/system/
    sudo systemctl daemon-reload
    sudo systemctl enable raspi2fb@1.service
    sudo systemctl start raspi2fb@1
    
# uninstall
    sudo systemctl stop raspi2fb@1
    sudo systemctl disable raspi2fb@1.service
    sudo rm /etc/systemd/system/raspi2fb@.service
    sudo rm /usr/local/bin/raspi2fb
