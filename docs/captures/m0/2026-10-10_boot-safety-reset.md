# Boot-safety capture, 2026-10-10

Siglent SDS1204X-E, 10x probes, DC 1M, 50 ms/div (700 ms window), 7 Mpts, 10 MSa/s.

| Ch | Signal | V/div | Offset |
|---|---|---|---|
| CH1 | GP21 = U2 /OE (Pico pin 27) | 2 V | +4.4 V |
| CH2 | CN104 pin 8 = LSTRB (B side) | 2 V | -1.0 V |
| CH3 | CN104 pin 4 = CA1 (B side) | 2 V | -7.0 V |

Mechanism supply on (+5V, +12V). Pico running the scaffold firmware.
Reset by `make reset` (OpenOCD `reset run` over SWD), confirmed by USB CDC
re-enumeration each time.

Three single-shot runs, one reset each, trigger level 1.52 V:

1. CH1 falling edge: no trigger. GP21 never went low.
2. CH2 rising edge: no trigger. No LSTRB pulse.
3. CH3 rising edge: no trigger. No CA1 pulse.

Screenshot is AUTO mode across a reset. CH1 mean 3.21 V, max 3.44 V,
400 mV p-p noise.
