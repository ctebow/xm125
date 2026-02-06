# Dual XM125 Sketch

Teensy sketch for two XM125 distance modules on I2C:

- **Front**: address 0x51, WAKE pin 20
- **Back**: address 0x52, WAKE pin 15

## Serial Protocol

Same protocol as `static_testing_sketch`, compatible with `benchmarking/scripts/serial_data_collection.py`:

- Send `START\n` to begin measurement loop
- Send `STOP\n` to end
- Each output line: `register rdistance strength` (space-separated)

**Register encoding:**

- `0–9`: Back sensor peaks 0–9  
- `10–19`: Front sensor peaks 0–9  

The Python script writes CSV: `trial, register, rdistance, strength, edistance`.

## Usage

1. Flash this sketch to a Teensy.
2. Wire both XM125 modules (front 0x51, back 0x52) and WAKE pins.
3. Run:

   ```bash
   python benchmarking/scripts/serial_data_collection.py -o output.csv -n 100 --num_trials 14 --edistance "200,190,180,170,160,150,140,130,120,110,100,90,80,70"
   ```
