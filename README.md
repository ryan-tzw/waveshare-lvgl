## Getting Started

### Embedded

1. Install the [Raspberry Pi Pico](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico) extension for VS Code

2. Configure the `Raspberry Pi Pico` extension ([refer to this](https://www.waveshare.com/wiki/RP2350-Touch-LCD-2#Configure_Extension)) (though I don't remember if I had to do this).

3. The `Raspberry Pi Pico Project` tab should appear in the activity bar on the left.   

4. With the project open, go to that tab, `Configure CMake` then `Compile Project`.

5. Flash the compiled `.uf2` file. 
  
    1. Enter bootloader mode.

        - Plug in the USB-C while holding the BOOT button, or
    
        - With the USB-C already plugged in, hold down the BOOT + RST buttons, let go of RST, then let go of BOOT.

    2. The RP2350 should appear as a storage device. Drop the `.uf2` file into it, and it will automatically flash and reboot into the program.

### Web


```bash
cd web

# Use either npm or pnpm

# Install project dependencies
npm install
pnpm install 

# Run dev server
npm run dev
pnpm run dev
```