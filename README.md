# snag
Copy primary display to a secondary framebuffer for display on a Sharp Memory LCD. Intended for use with [Beeperberry](https://beepberry.sqfmi.com) to enable fast copying of HDMI data to its display.

Forked from https://github.com/AndrewFromMelbourne/raspi2fb with the following changes:
* copies 16-bit HDMI display to 1-bit Sharp Memory Display buffer
* converts coloured buffer to grayscale then 1-bit using Bayer dithering to maintain spatial integrity/accuracy of any black & white data

## Some rather important notes
1. ~~snag _will not work_ on the~~ Beepberry-recommended Raspberry Pi Lite image. This is a 'Bullseye' image, which dropped Dispmanx and the API we're using here. I'm making a new version (from scratch with little knowledge of C, so bear with me!), but in the meantime, the options are:
   * Use a 'Legacy' version of Raspberry Pi OS. These are 'Buster' images, which include Dispmanx.
   * Use a Retropie image, as these are also built on Buster.
     
I would recommend the second option, as it includes a pre-compiled and working SDL2.

**Edit 20230626:** After quite some pissing about with this and making slow but steady progress, I've discovered that although DispmanX is deprecated, the legacy and 'Fake KMS' drivers in Bullseye use it.

You can fall back to the legacy driver by commenting out this line in /boot/config.txt:

```#dtoverlay=vc4-kms-v3d```

This will cause Raspberry Pi OS to use Dispmanx, and snag will run. Alternatively, you might try:

```dtoverlay=vc4-fkms-v3d```

## Dithering examples
![DitherPatterns](https://github.com/TheMediocritist/snag/assets/79881777/9cbcde9c-946f-45ee-acaa-2af6b710ca7c)

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

### Notes
1. By default, Beepberry is set up to display the linux console on the Sharp framebuffer. If you see flickering text or a cursor, that's because **snag** and **fbcon** are both writing to the same framebuffer. You can fix this by removing `fbcon=map:10` from /boot/cmdline.txt (you may need to use `ssh` to re-enable it).
2. Although I've tried my best to make **snag** efficient, it still has to churn through 96,000 pixels per update and this comes with a cost. At the default target of 30fps it will consume somewhere between 10% to 20% of the processing power of a Raspberry Pi Zero depending on what's drawing to the screen. If this doesn't work for you, you could: reduce the target FPS; try a Pi Zero 2 or Radxa Zero; or improve the code and submit a PR.
3. Coloured terminal fonts can be difficult to read. Try this:

    ```setterm --inversescreen=off -background=white -foreground=black -store```
    
   This will probably leave you with a patterned background, because the terminal's version of white is actually a light gray. Set it to true white with:
    
    ```echo -en "\e]P7ffffff"```
    
   You can add this line to bash so it automatically runs:
   
    ``` sudo nano ~/.bashrc```

## How to build

1. Install the build tool cmake and the libbsd-dev library
    ```
    sudo apt-get install cmake
    sudo apt-get install libbsd-dev
    ```
2. Download this repo
    ```
    git clone https://github.com/TheMediocritist/snag
    ```
4. Build the executable
    ```
    cd snag
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
5. (optional) Run as a system service - this will persist after reboot unless the uninstall procedure is followed.
    ```
    sudo make install
    sudo cp ../snag.init.d /etc/init.d/snag
    sudo update-rc.d snag defaults
    sudo service snag start
    ```
### How to uninstall

1. Stop and remove the system service (if setup)
    ```sudo service snag stop
    sudo update-rc.d -f snag remove
    ```
2. Delete the executable file
    ```
    sudo rm /usr/local/bin/snag
    sudo rm /etc/init.d/snag
    ```
