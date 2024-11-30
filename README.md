## Hello Computer  

Dev material for the Music Thing Modular Computer, part of the Workshop System  

 
### Important: Revisions of the Computer Hardware
Check the back of your Computer PCB to see what revision you have. 

**Proto 2.0.1 / Rev 1.0.0** 
This is the September 2024 version, avaliable with the Proto 2 systems used at Machina Bristronica and shipped in first batch of systems
- Fixed LED issues from Proto 2.0
- Added 8k i2c EEPROM on GPIO16 (SDA) and GPIO17 (SCL)
    - Connects to Zetta ZD24C08A EEPROM, clone of 24C0* chips. Contains 8K bits (1024 X 8). 
    - SDA & SCL lines have 2.2k pullups. 
    - Data is stored in 8 x 1024 bit pages addresses 0x50 to 0x5B
- Cosmetic silkscreen changes between Proto 2.0.1 and Rev 1.0.0

**Proto 2.0** 
This is the July 2024 version, available in very limited numbers due to wonky LEDs 
- Cleaner power for RP2040 ADC: 80hz low pass RC filter on the ADC power pin (200Ω + 10uF to ground) 
- Filtering on Audio inputs to ADC: 16kHz RC filters (1KΩ + 10nF to ground) 
- Normalisation probe: GPIO4 is attached to all socket switched inputs via 100K resistors and BAT54S protection diodes. 
- Board identidfication bits: GPIO Pins 7, 6, 5 = binary bits connected to +3.3 or GND  
  - 0 0 0 = Previous boards (pins floating)
  - 1 0 0 = Proto 2.0 this board 
- Improved PWM filtering: changed from Sallen Key to Multifeedback for reduced 60kHz ripple
  - NB: **PWM CV outputs are now inverted**
- SMD LEDs: 0603 red LEDs on the board (together with thru-hole footprints) so this layout could be used with light pipes vs 3mm leds. 
- LED Buffers: Incorrect use of CD4050BPWR, so **LEDs don't work at all on this board**


**Proto 1.2**
  - This is the May 2024 version shipped out as a Developer Kit to more people
  - DAC Issues are corrected: For this version, Gain should be set to 1x like this:
    - ```#define DAC_config_chan_A_nogain 0b0011000000000000```
    - And the output will be -6v to +6v. If you use 2x gain, the output will swing from +6 to -11v
  - Other changes
    - This version has GPIO Pins 0 and 1 - the default UART pins - pulled out to two pins by the LEDs
    - Other changes were non-functional layout changes -- I'd misunderstood the JLCPCB design rules so the components in Proto 1 were a bit tight

- **Proto 1**
  - This is the version in all the Dyski Workshop systems, and the version that the very earliest developers have
  - DAC Issues: In this version the MCP4822 gain structure is incorrect, so the gain needed to be set to 'double' and the output was not centered properly.
  - For example ```#define DAC_config_chan_A_gain 0b0001000000000000```


#### Board identification code 

```
int versionCheck[3] = {5, 6, 7}; // Pins 5,6,7 set a 3-bit hardware version number


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  for (int i = 0; i < 3; i++) {
    pinMode(versionCheck[i], INPUT_PULLDOWN);
  }
}

void loop() {
  int version = (digitalRead(versionCheck[2]) << 2) | (digitalRead(versionCheck[1]) << 1) | digitalRead(versionCheck[0]);
  Serial.print("Version is ");
  Serial.println(version);
  delay(1000);
}

// Prints 0 for old boards, 1 for new boards 
```

### Demonstrations+HelloWorlds

- Starter code in various platforms - Arduino, Pico SDK, need one for Circuit or MicroPython and Rust 

### RELEASES 


My suggestion for the first 100 projects is that people grab numbers & folders in the 'releases' folder - by sending pull requests - then share whatever they're comfortable sharing - uf2, source, just documentation, or just a link to your own repo/web/gists, whatever works best. I don't have much experience of this kind of collective development, so would be delighted if better approach emerges.  

Release documentation: I've been making little leaflets for each card, designed in Google Sheets  
- https://docs.google.com/presentation/d/19z0S9cpGnyhb7lVmBPHYjTZLpEB-Xg-v9zzfXCjCjOQ/copy   
- https://docs.google.com/presentation/d/10R8onfP5JAq9MpOgVSa4sAhxg-WTx7_0-Q1fY0MUDho/copy

### DOCUMENTATION 

Contains schematic and initial documentation, pinouts etc. 
  
