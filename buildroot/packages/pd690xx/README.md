# PD690xx PoE library

This library controls Microsemi/Microchip PD690xx PoE controllers used by PoE-capable Meraki switches.

## Capabilities

- probe and count responding controllers;
- read controller temperatures;
- read per-port power, state, and mode;
- enable or disable a port;
- select 802.3af (`PORT_MODE_AF`) or 802.3at (`PORT_MODE_AT`).

## Port rules

Logical port mapping is bounded by detected controllers and the model-specific PoE copper-port count. SFP/SFP+ ports and non-PoE switch models must never be passed to PoE operations.

`configd` separates:

- hardware **support**: model has PoE ports;
- current **availability**: one or more controllers responded.

A temporary probe failure does not invalidate persistent desired state. Operations return errors and configd reports warnings.

## Example

```c
if (port_set_type(&cfg, 1, PORT_MODE_AT) == 0)
    port_enable(&cfg, 1);
```

Callers must close I2C file descriptors with `i2c_close()` during shutdown.
