# Recovery flat-binary entry contract

meraki-redboot stage 1 loads a recovery flat binary at `0x81000000` and jumps to byte zero of that binary. Therefore ELF entry metadata alone is not sufficient: MIPS metadata sections must not precede executable startup code in the `objcopy -O binary` output.

The recovery source uses a dedicated `entry.S` in `.text.start`, links it first, discards non-runtime MIPS metadata, asserts `_start == 0x81000000`, initializes stack/GP/BSS, and calls `recovery_main`. Every generated descriptor and release manifest declares:

```text
entry_contract: flat-binary-byte-zero-v1
```

The authoritative meraki-redboot repository owns this contract. The builder fetches the selected upstream revision, verifies the declaration and structural build, and refuses to emit a loader or complete image when the contract is absent. The builder never patches the upstream checkout.

Menu option 1 uploads a manifest-matched external family recovery stage and is the default host path. Menu option 2 executes the embedded stage only when the installed loader advertises the required entry contract and digest.
