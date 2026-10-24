# Device and capability reference

[Overview](../README.md) · [Command-line use](command-line.md) · [Programming](programming.md)

## Catalogue coverage

The catalogue contains Kindle 4 and later models and Kobo Touch and later
models, including regional and storage variants identified by model codes.
Pre-Kindle-4 models are outside its scope. Use `ereader-hal models` for the
exact names in your installed release.

Catalogue information describes hardware and firmware facts. Runtime control
support is narrower and depends on the firmware interfaces available to the HAL.
A listed model is not a claim that every feature has been tested or implemented.

`ereader-hal info 'Kobo Sage'`, Python `model_info('Kobo Sage')`, and C
`er_get_model_info()` read static facts without opening hardware. Static panel
dimensions are zero and suspend mode is `unknown`. Opening a device reads its
framebuffer dimensions when available and the kernel's supported suspend mode.

`DeviceInfo.features` reports navigation keys, D-pad, firmware keyboard,
multitouch, ambient-light sensor, natural-light mixer, auxiliary battery,
charging LED, and whether native brightness can turn the light off. These do
not imply that the HAL offers controls for each feature.

## Capability results

Use `get_capability()` / `er_get_capability()` for a control's current state.

| Field | Meaning |
| --- | --- |
| `supported` | The device profile reports the feature. |
| `implemented` | The HAL has a binding for this model and interface. |
| `available` | The probe found the control usable now. |
| `blocked` | The probe found a busy resource or radio-off condition. |
| `reason_code`, `reason` | A machine-readable reason and an explanation. |
| `error` | Positive `errno`, or zero on success. |
| `minimum`, `maximum`, `steps` | Range information for numeric controls. |

An unavailable control is not necessarily marked `blocked`: missing
permissions, services, or drivers have their own reasons. Availability is a
snapshot; an operation can still fail after a successful probe.

## Model detection

- **Kindle:** reads the model code from `/proc/usid`.
- **Kobo:** uses `MODEL_NUMBER` or `/mnt/onboard/.kobo/version`. It accepts
  firmware `PRODUCT` codenames when no product ID is available.
- **Kobo Touch A/B:** both use ID 310. Detection also needs the firmware
  `PLATFORM`, or a CPU value reported by `ntx_hwconfig`.

Unknown or ambiguous identities make `er_open()` return `-ENODEV`; Python raises
`OSError`. Use catalogue lookups to inspect a different model. Setting identity
environment variables does not simulate its hardware.

## Known limits

- Automatic-brightness control is not implemented, including on models with
  an ambient-light sensor.
- Kindle orientation-event bindings are not implemented.
- Kobo Touch A/B and Allwinner display/input bindings are incomplete. Rotation,
  touch, and orientation controls can report `not-implemented`.
- Kobo radio power and light mixing depend on the detected module and driver.
  Some hardware combinations lack a binding.
- USB connection status requires kernel UDC and power-supply interfaces that
  older firmware may not expose. Custom gadgets need additional kernel support.
- Rendering, screen refresh, waveform selection, dithering, and refresh-completion
  waits are outside the HAL. Rotation does not redraw pixels.
- A hardware feature such as a stylus, auxiliary battery, or charging LED is
  not a promise of a dedicated control API.

The catalogue does not assign unsupported distinctions to unresolved variants,
including Paperwhite 6 Signature and Scribe 3 lighting variants.

## Sources and attribution

Device identifiers and hardware facts follow KOReader's definitions at revision
`afff28b107a7ea4ab5867b5e65dd054987ca1d99`:

- [Kindle device definitions](https://github.com/koreader/koreader/blob/afff28b107a7ea4ab5867b5e65dd054987ca1d99/frontend/device/kindle/device.lua)
- [Kobo device definitions](https://github.com/koreader/koreader/blob/afff28b107a7ea4ab5867b5e65dd054987ca1d99/frontend/device/kobo/device.lua)

Rotation maps credit [FBInk](https://github.com/NiLuJe/FBInk). The corresponding
HAL tables are in [kindle.c](../src/profiles/kindle.c) and
[kobo.c](../src/profiles/kobo.c). Keep these attributions with changes to the tables.

KOReader and FBInk are separate projects. The HAL does not install either of
them, and this attribution does not imply that KOReader uses this HAL.
Their code and assets retain their own license terms.
