# Cooling notes

One-rack-unit models use active cooling and postmerkOS enables their fans during boot. Keep airflow paths clear and verify temperatures after any fan modification.

MS22/MS42 models commonly use 12 V 40 mm fans. MS220-24/48 models commonly use a four-wire 12 V blower with:

| Pin | Signal |
|---:|---|
| 1 | GND |
| 2 | PWM control |
| 3 | 12 V |
| 4 | RPM sense |

Some switch boards drive these blowers as simple on/off fans and do not provide usable software PWM. An external temperature-controlled PWM module is safer than reducing voltage blindly. Attach temperature probes securely to the principal heatsink and confirm adequate airflow under PoE and high-port-load conditions.
