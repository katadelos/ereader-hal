"""Hardware capabilities and value snapshots returned by the HAL."""

from dataclasses import dataclass, field
from enum import IntEnum, IntFlag


class Capability(IntFlag):
    BRIGHTNESS = 1 << 0
    WARMTH = 1 << 1
    WIFI = 1 << 2
    BATTERY = 1 << 3
    DISPLAY = 1 << 4
    RTC_WAKE = 1 << 5
    SUSPEND = 1 << 6
    ROTATION = 1 << 7
    TOUCH = 1 << 8
    BUTTONS = 1 << 9
    COVER = 1 << 10
    ORIENTATION = 1 << 11
    POWER_EVENTS = 1 << 12
    USB = 1 << 13
    FRONTLIGHT = 1 << 14
    AUTO_BRIGHTNESS = 1 << 15
    INHIBIT_HAL = 1 << 16
    INHIBIT_SYSTEM = 1 << 17
    WIFI_STATUS = 1 << 18
    WIFI_SCAN = 1 << 19
    WIFI_PROFILES = 1 << 20
    WIFI_CONNECT = 1 << 21
    AWAKE_LEASE = 1 << 22
    BATTERY_DETAILS = 1 << 23


class CapabilityReason(IntEnum):
    NONE = 0
    UNSUPPORTED = 1
    NOT_IMPLEMENTED = 2
    PERMISSION = 3
    DRIVER_MISSING = 4
    SERVICE_UNAVAILABLE = 5
    RADIO_OFF = 6
    RESOURCE_BUSY = 7
    TIMEOUT = 8
    IO_ERROR = 9
    INVALID_CONFIGURATION = 10


class ChargeState(IntEnum):
    UNKNOWN = 0
    CHARGING = 1
    DISCHARGING = 2
    FULL = 3
    NOT_CHARGING = 4


class USBConnectionState(IntEnum):
    DISCONNECTED = 0
    CONNECTED = 1
    CHARGING = 2
    DATA = 3


class USBMode(IntFlag):
    FILESTORAGE = 1 << 0  # Status only; storage belongs to firmware.
    NETWORK = 1 << 1
    ADB = 1 << 2
    HID = 1 << 3
    SERIAL = 1 << 4


class USBModeState(IntEnum):
    ACTIVE = 0
    PENDING = 1
    BLOCKED = 2


class USBFunction(IntFlag):
    STORAGE = 1 << 0  # Observed firmware storage; cannot be configured.
    ECM = 1 << 1
    RNDIS = 1 << 2
    ADB = 1 << 3
    HID = 1 << 4
    SERIAL = 1 << 5


@dataclass(frozen=True)
class USBConfig:
    functions: USBFunction = USBFunction(0)
    vendor_id: int = 0
    product_id: int = 0
    manufacturer: str | None = None
    product: str | None = None
    serial: str | None = None
    device_mac: str | None = None
    host_mac: str | None = None
    hid_descriptor: bytes = b""
    hid_report_length: int = 0
    hid_protocol: int = 0
    hid_subclass: int = 0
    take_stock_usb: bool = False


@dataclass(frozen=True)
class USBGadget:
    functions: USBFunction
    prepared: bool
    bound: bool
    owned: bool
    controller: str
    gadget: str
    state: str
    network_interface: str
    adb_path: str


@dataclass(frozen=True)
class USBTransition:
    requested: USBFunction
    active: USBFunction
    prepared: bool
    reenumerated: bool
    restored: bool
    cleanup_pending: bool
    error: int
    cleanup_error: int
    restore_error: int


@dataclass(frozen=True)
class USBModeTransition:
    requested: USBMode
    active: USBMode
    prepared: bool
    reenumerated: bool
    restored: bool
    cleanup_pending: bool
    error: int
    cleanup_error: int
    restore_error: int
    state: USBModeState


class Button(IntEnum):
    PREVIOUS = 0
    NEXT = 1
    POWER = 2
    HOME = 3
    BACK = 4
    MENU = 5
    UP = 6
    DOWN = 7
    LEFT = 8
    RIGHT = 9
    SELECT = 10


class Action(IntEnum):
    RELEASE = 0
    PRESS = 1
    REPEAT = 2
    MOVE = 3
    CANCEL = 4


class WakeReason(IntEnum):
    UNKNOWN = 0
    RTC = 1
    POWER = 2
    COVER = 3


class EventType(IntEnum):
    BUTTON = 0
    TOUCH = 1
    COVER = 2
    ORIENTATION = 3
    BATTERY = 4
    LOW_BATTERY = 5
    USB = 6
    PREPARE_SLEEP = 7
    RESUMED = 8
    SUSPEND_FAILED = 9
    INPUT_RESET = 10
    STATUS_ERROR = 11
    WIFI = 12
    CAPABILITY = 13


@dataclass(frozen=True)
class CapabilityInfo:
    supported: bool
    implemented: bool
    available: bool
    blocked: bool
    error: int
    minimum: int
    maximum: int
    steps: int
    reason_code: CapabilityReason
    reason: str


@dataclass(frozen=True)
class Features:
    """Catalogue facts, independent of runtime control availability."""

    navigation_keys: bool
    dpad: bool
    firmware_keyboard: bool
    multitouch: bool
    light_sensor: bool
    natural_light_mixer: bool
    auxiliary_battery: bool
    charging_led: bool
    native_light_off: bool


@dataclass(frozen=True)
class DeviceInfo:
    model: str
    codes: str
    codenames: str
    capabilities: Capability
    panel_width: int
    panel_height: int
    dpi: int
    color: bool
    touch: bool
    stylus: bool
    page_buttons: bool
    power_button: bool
    cover: bool
    orientation_sensor: bool
    suspend_mode: str
    features: Features


@dataclass(frozen=True)
class DeviceIdentity:
    serial: str
    wifi_interface: str
    wifi_mac: str


@dataclass(frozen=True)
class Battery:
    percent: int
    state: ChargeState


@dataclass(frozen=True)
class Display:
    width: int
    height: int
    bits_per_pixel: int
    rotation: int


@dataclass(frozen=True)
class Frontlight:
    enabled: bool
    brightness: int
    warmth: int
    restore_brightness: int


@dataclass(frozen=True)
class USB:
    state: USBConnectionState
    external_power: bool
    storage_mounted: bool
    storage_writable: bool


@dataclass(frozen=True)
class Wake:
    reason: WakeReason
    irq: int


@dataclass(frozen=True)
class ButtonInput:
    button: Button
    action: Action


@dataclass(frozen=True)
class TouchInput:
    id: int
    x: int
    y: int
    action: Action


@dataclass(frozen=True)
class Failure:
    capability: Capability
    error: int


@dataclass(frozen=True)
class Event:
    type: EventType
    data: (
        ButtonInput
        | TouchInput
        | bool
        | int
        | Battery
        | USB
        | Wake
        | Failure
        | WifiStatus
        | CapabilityChange
        | None
    )


class WifiSecurity(IntEnum):
    OPEN = 0
    WPA_PSK = 1
    SAE = 2
    WEP = 3
    ENTERPRISE = 4
    UNKNOWN = 5


class WifiSecurityMask(IntFlag):
    OPEN = 1 << WifiSecurity.OPEN
    WPA_PSK = 1 << WifiSecurity.WPA_PSK
    SAE = 1 << WifiSecurity.SAE
    WEP = 1 << WifiSecurity.WEP
    ENTERPRISE = 1 << WifiSecurity.ENTERPRISE
    UNKNOWN = 1 << WifiSecurity.UNKNOWN


class WifiCompletion(IntEnum):
    CONFIRMED = 0
    FAILED = 1
    UNCERTAIN = 2


@dataclass(frozen=True)
class WifiCapabilities:
    security_read: WifiSecurityMask
    security_write: WifiSecurityMask
    hidden: bool
    enabled: bool
    rename: bool


class WifiState(IntEnum):
    OFF = 0
    DISCONNECTED = 1
    CONNECTING = 2
    CONNECTED = 3


@dataclass(frozen=True)
class WifiNetwork:
    id: str
    ssid: str
    security: WifiSecurity
    hidden: bool | None
    enabled: bool | None
    current: bool


@dataclass(frozen=True)
class WifiConfig:
    ssid: str
    security: WifiSecurity = WifiSecurity.WPA_PSK
    hidden: bool = False
    enabled: bool = True
    password: str | None = field(default=None, repr=False)


@dataclass(frozen=True)
class WifiAccessPoint:
    ssid: str
    bssid: str
    security: WifiSecurity
    signal_dbm: int | None
    frequency_mhz: int


@dataclass(frozen=True)
class WifiStatus:
    state: WifiState
    network_id: str
    ssid: str
    bssid: str
    address: str
    signal_dbm: int | None


@dataclass(frozen=True)
class BatteryDetails:
    voltage_uv: int | None
    current_ua: int | None
    temperature_millicelsius: int | None
    charge_uah: int | None
    full_uah: int | None
    design_uah: int | None
    cycle_count: int | None
    health: str | None


@dataclass(frozen=True)
class CapabilityChange:
    capability: Capability
    info: CapabilityInfo


@dataclass(frozen=True)
class WifiResult:
    completion: WifiCompletion
    error: int
    network_id: str
    status: WifiStatus | None
