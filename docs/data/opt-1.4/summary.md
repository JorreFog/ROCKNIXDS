## Idle menu

| | 1.3 | 1.4 |
|---|---|---|
| CPU used (% of one core) | 56.6 | 9.8 |
| Average CPU clock (MHz) | 1992 | 457 |
| Average GPU clock (MHz) | 400 | 200 |
| SoC temperature at the end (°C) | 52.1 | 47.8 |
| Processes started per second | 18.6 | 3.4 |
| Battery current (mA, on the charger, + = charging) | -101 | +82 |

| Where it goes (% of one core) | 1.3 | 1.4 |
|---|---|---|
| Drawing the menu (ES, sway) | 35.0 | 0.6 |
| Audio (PipeWire) | 4.0 | 0.0 |
| ROCKNIX services | 7.1 | 2.0 |
| Kernel and the rest | 10.5 | 7.2 |

## HeartGold at 2x (median of 3 runs)

| Shader | Version | CPU used (% of one core) | Avg CPU clock (MHz) | Avg GPU clock (MHz) | SoC rise in 90 s (°C) | Drops/s | Audio (% of one core) | ROCKNIX services (% of one core) | Battery (mA) |
|---|---|---|---|---|---|---|---|---|---|
| no shader | 1.3 | 124 | 1992 | 0 | +1.7 | 0.02 | 8.4 | 8.1 | -258 |
| no shader | 1.4 | 135 | 1445 | 0 | +1.1 | 0.02 | 4.8 | 3.2 | -240 |
| ds-crisp | 1.3 | 156 | 1992 | 400 | +1.8 | 0.07 | 9.5 | 9.0 | -414 |
| ds-crisp | 1.4 | 146 | 1567 | 400 | -0.6 | 0.04 | 5.1 | 3.2 | -308 |
| sharp-bilinear | 1.3 | 159 | 1992 | 400 | +1.2 | 0.09 | 9.5 | 9.0 | -417 |
| sharp-bilinear | 1.4 | 147 | 1662 | 400 | +0.0 | 0.11 | 5.1 | 3.2 | -315 |

Dropped frames per run:

- no shader, 1.3: 0.02, 3.76, 0.00
- no shader, 1.4: 0.04, 0.02, 0.00
- ds-crisp, 1.3: 0.07, 0.09, 0.02
- ds-crisp, 1.4: 0.04, 0.04, 0.07
- sharp-bilinear, 1.3: 0.09, 0.04, 0.16
- sharp-bilinear, 1.4: 0.00, 0.11, 0.15

## Stress ROM

Last 30 s (heaviest levels): 1.3 50.6 fps, 1.4 51.9 fps. Average clock: 1.3 1992 MHz, 1.4 1795 MHz.

## Shaders (ms per panel at 800 MHz)

| Shader | 1.3, 2x | 1.4, 2x | 1.3, 1x | 1.4, 1x |
|---|---|---|---|---|
| null | 1.96 | 0.53 | 1.05 | 0.45 |
| ds-crisp | 1.94 | 0.57 | 1.11 | 0.49 |
| sharp-bilinear | 1.68 | 0.57 | 0.89 | 0.47 |
| sharp-shimmerless | 1.69 | 0.57 | 0.88 | 0.50 |
| scanlines | 1.66 | 0.58 | 0.92 | 0.53 |
| quilez | 1.70 | 0.60 | 0.90 | 0.52 |
| lcd3x | 1.70 | 0.69 | 0.91 | 0.69 |
| ds-grid-2x | 1.89 | 0.75 | 0.90 | 0.75 |
| ds-grid | 1.65 | 1.00 | 1.12 | 1.00 |
| ds-crisp-color | 1.94 | 1.07 | 1.66 | 1.07 |
| lcd1x-nds-color | 1.66 | 1.65 | 1.64 | 1.64 |
| ds-grid-color | 2.02 | 1.68 | 2.01 | 1.68 |

## Clock sweep (development runs)

| Setting | Clock (MHz) | Drops/s per run |
|---|---|---|
| no shader, fixed | 1104 | 0.13 |
| no shader, fixed | 1416 | 0.07 |
| no shader, fixed | 1608 | 0.10 |
| no shader, fixed | 1992 | 0.00, 0.07 |
| ds-crisp, fixed | 1104 | 0.90, 0.26 |
| ds-crisp, fixed | 1416 | 0.44, 0.24 |
| ds-crisp, fixed | 1992 | 0.00, 0.04 |
| no shader, 1.4 governor | 1622 (average) | 0.04 |
| ds-crisp, 1.4 governor | 1778 (average) | 0.07 |
| no shader, schedutil | 1771 (average) | 0.10 |

## Switching (seconds, median of 3 cycles)

| | 1.3 | 1.4 |
|---|---|---|
| Launch: libdsflip has the display | 2.83 | 2.85 |
| Launch: first frame | 3.44 | 3.47 |
| Quit: the menu answers | 2.65 | 3.51 |
| Quit: the menu is visible | 4.47 | 5.26 |

