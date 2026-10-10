# Pico power cycle with the mechanism powered, 2026-10-10

Siglent SDS1204X-E, 10x probes, DC 1M, 2 V/div on all channels. Trigger:
CH2 (LSTRB) rising, 1.52 V, single shot. Mechanism supply on throughout.
The Pico was power-cycled by unplugging and replugging its USB.

| Ch | Signal | Offset |
|---|---|---|
| CH1 | GP21 = U2 /OE (tracks Pico 3V3 when unpowered) | +4.4 V |
| CH2 | CN104 pin 8 = LSTRB | -1.0 V |
| CH3 | CN104 pin 4 = CA1 (first unplug), then pin 14 = /ENBL | -7.0 V |

| File | Event | Timebase |
|---|---|---|
| `2026-10-10_usb-unplug-50ms.png` | Unplug, CH3 = CA1 | 50 ms/div |
| `2026-10-10_usb-unplug-5ms.png` | Same capture, zoomed | 5 ms/div |
| `2026-10-10_usb-unplug-enbl.png` | Second unplug, CH3 = /ENBL | 5 ms/div |
| `2026-10-10_usb-plug-5ms.png` | Replug, CH3 = /ENBL | 5 ms/div |
| `2026-10-10_usb-plug-2us.png` | Same capture, zoomed | 2 us/div |

Results:

- Unplug: 3V3 falls to about 1.3 V in about 3 ms, then sags. When it reaches
  about 0.8 V, U2 drives LSTRB and CA1 to 5 V for about 1.5 ms. /ENBL dips
  to 3.64 V minimum and does not go low. Repeated twice, same result.
- Replug: as 3V3 rises through about 1 V, U2 drives LSTRB to 5 V for about
  1.3 us. It then decays through the 10k pull-down. /ENBL stays at 4.76 V or
  above.
- Mechanism silent in all runs.
