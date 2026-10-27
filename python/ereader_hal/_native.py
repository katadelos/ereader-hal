"""C ABI declarations and conversion to independent Python snapshots."""

import ctypes as ct
import os
from dataclasses import fields

from . import types


class CapabilityInfo(ct.Structure):
    _fields_ = [
        ("supported", ct.c_bool),
        ("implemented", ct.c_bool),
        ("available", ct.c_bool),
        ("blocked", ct.c_bool),
        ("error", ct.c_int),
        ("minimum", ct.c_int),
        ("maximum", ct.c_int),
        ("steps", ct.c_int),
        ("reason_code", ct.c_int),
        ("reason", ct.c_char * 128),
    ]


class Features(ct.Structure):
    _fields_ = [
        ("navigation_keys", ct.c_bool),
        ("dpad", ct.c_bool),
        ("firmware_keyboard", ct.c_bool),
        ("multitouch", ct.c_bool),
        ("light_sensor", ct.c_bool),
        ("natural_light_mixer", ct.c_bool),
        ("auxiliary_battery", ct.c_bool),
        ("charging_led", ct.c_bool),
        ("native_light_off", ct.c_bool),
    ]


class Info(ct.Structure):
    _fields_ = [
        ("model", ct.c_char_p),
        ("codes", ct.c_char_p),
        ("codenames", ct.c_char_p),
        ("capabilities", ct.c_uint32),
        ("panel_width", ct.c_uint),
        ("panel_height", ct.c_uint),
        ("dpi", ct.c_uint),
        ("color", ct.c_bool),
        ("touch", ct.c_bool),
        ("stylus", ct.c_bool),
        ("page_buttons", ct.c_bool),
        ("power_button", ct.c_bool),
        ("cover", ct.c_bool),
        ("orientation_sensor", ct.c_bool),
        ("suspend_mode", ct.c_char_p),
        ("features", Features),
    ]


class Identity(ct.Structure):
    _fields_ = [
        ("serial", ct.c_char * 64),
        ("wifi_interface", ct.c_char * 16),
        ("wifi_mac", ct.c_char * 18),
    ]


class Battery(ct.Structure):
    _fields_ = [("percent", ct.c_int), ("state", ct.c_int)]


class Display(ct.Structure):
    _fields_ = [
        ("width", ct.c_uint),
        ("height", ct.c_uint),
        ("bits_per_pixel", ct.c_uint),
        ("rotation", ct.c_uint),
    ]


class Frontlight(ct.Structure):
    _fields_ = [
        ("enabled", ct.c_bool),
        ("brightness", ct.c_int),
        ("warmth", ct.c_int),
        ("restore_brightness", ct.c_int),
    ]


class USB(ct.Structure):
    _fields_ = [
        ("state", ct.c_int),
        ("external_power", ct.c_bool),
        ("storage_mounted", ct.c_bool),
        ("storage_writable", ct.c_bool),
    ]


class USBGadget(ct.Structure):
    _fields_ = [
        ("functions", ct.c_uint32),
        ("prepared", ct.c_bool),
        ("bound", ct.c_bool),
        ("owned", ct.c_bool),
        ("controller", ct.c_char * 64),
        ("gadget", ct.c_char * 256),
        ("state", ct.c_char * 32),
        ("network_interface", ct.c_char * 16),
        ("adb_path", ct.c_char * 128),
    ]


class USBConfig(ct.Structure):
    _fields_ = [
        ("functions", ct.c_uint32),
        ("vendor_id", ct.c_uint16),
        ("product_id", ct.c_uint16),
        ("manufacturer", ct.c_char_p),
        ("product", ct.c_char_p),
        ("serial", ct.c_char_p),
        ("device_mac", ct.c_char_p),
        ("host_mac", ct.c_char_p),
        ("hid_descriptor", ct.POINTER(ct.c_uint8)),
        ("hid_descriptor_size", ct.c_size_t),
        ("hid_report_length", ct.c_uint),
        ("hid_protocol", ct.c_uint),
        ("hid_subclass", ct.c_uint),
        ("take_stock_usb", ct.c_bool),
    ]


class USBTransition(ct.Structure):
    _fields_ = [
        ("requested", ct.c_uint32),
        ("active", ct.c_uint32),
        ("prepared", ct.c_bool),
        ("reenumerated", ct.c_bool),
        ("restored", ct.c_bool),
        ("cleanup_pending", ct.c_bool),
        ("error", ct.c_int),
        ("cleanup_error", ct.c_int),
        ("restore_error", ct.c_int),
    ]


class USBModeTransition(ct.Structure):
    _fields_ = USBTransition._fields_ + [("state", ct.c_int)]


class Wake(ct.Structure):
    _fields_ = [("reason", ct.c_int), ("irq", ct.c_int)]


class ButtonInput(ct.Structure):
    _fields_ = [("button", ct.c_int), ("action", ct.c_int)]


class TouchInput(ct.Structure):
    _fields_ = [("id", ct.c_int), ("x", ct.c_int), ("y", ct.c_int), ("action", ct.c_int)]


class Failure(ct.Structure):
    _fields_ = [("capability", ct.c_int), ("error", ct.c_int)]


class WifiNetwork(ct.Structure):
    _fields_ = [
        ("id", ct.c_char * 128),
        ("ssid", ct.c_char * 33),
        ("security", ct.c_int),
        ("hidden", ct.c_bool),
        ("enabled", ct.c_bool),
        ("current", ct.c_bool),
        ("hidden_known", ct.c_bool),
        ("enabled_known", ct.c_bool),
    ]


class WifiConfig(ct.Structure):
    _fields_ = [
        ("ssid", ct.c_char * 33),
        ("security", ct.c_int),
        ("hidden", ct.c_bool),
        ("enabled", ct.c_bool),
        ("password", ct.c_char_p),
    ]


class WifiAccessPoint(ct.Structure):
    _fields_ = [
        ("ssid", ct.c_char * 33),
        ("bssid", ct.c_char * 18),
        ("security", ct.c_int),
        ("signal_dbm", ct.c_int),
        ("frequency_mhz", ct.c_uint),
    ]


class WifiStatus(ct.Structure):
    _fields_ = [
        ("state", ct.c_int),
        ("network_id", ct.c_char * 128),
        ("ssid", ct.c_char * 33),
        ("bssid", ct.c_char * 18),
        ("address", ct.c_char * 46),
        ("signal_dbm", ct.c_int),
    ]


class WifiResult(ct.Structure):
    _fields_ = [
        ("completion", ct.c_int),
        ("error", ct.c_int),
        ("network_id", ct.c_char * 128),
        ("status_known", ct.c_bool),
        ("status", WifiStatus),
    ]


class WifiCapabilities(ct.Structure):
    _fields_ = [
        ("security_read", ct.c_uint32),
        ("security_write", ct.c_uint32),
        ("hidden", ct.c_bool),
        ("enabled", ct.c_bool),
        ("rename", ct.c_bool),
    ]


class CapabilityChange(ct.Structure):
    _fields_ = [("capability", ct.c_int), ("info", CapabilityInfo)]


class BatteryDetails(ct.Structure):
    _fields_ = [
        ("present", ct.c_uint32),
        ("voltage_uv", ct.c_int),
        ("current_ua", ct.c_int),
        ("temperature_millicelsius", ct.c_int),
        ("charge_uah", ct.c_int),
        ("full_uah", ct.c_int),
        ("design_uah", ct.c_int),
        ("cycle_count", ct.c_int),
        ("health", ct.c_char * 32),
    ]


class EventData(ct.Union):
    _fields_ = [
        ("button", ButtonInput),
        ("touch", TouchInput),
        ("cover_closed", ct.c_bool),
        ("orientation", ct.c_uint),
        ("battery", Battery),
        ("usb", USB),
        ("wake", Wake),
        ("failure", Failure),
        ("wifi", WifiStatus),
        ("capability", CapabilityChange),
    ]


class Event(ct.Structure):
    _fields_ = [("type", ct.c_int), ("data", EventData)]


LifecycleHandler = ct.CFUNCTYPE(ct.c_int, ct.POINTER(Event), ct.c_void_p)


def load(path=None):
    path = path if path is not None else os.environ.get("EREADER_HAL_LIBRARY", "libereader-hal.so")
    lib = ct.CDLL(os.fspath(path))

    def bind(name, *arguments, result=ct.c_int):
        function = getattr(lib, name)
        function.restype = result
        function.argtypes = arguments

    device = ct.c_void_p
    bind("er_model_name", ct.c_uint, result=ct.c_char_p)
    bind("er_get_model_info", ct.c_uint, ct.POINTER(Info))
    bind("er_capability_name", ct.c_int, result=ct.c_char_p)
    bind("er_capability_reason_name", ct.c_int, result=ct.c_char_p)
    bind("er_open", ct.POINTER(device))
    bind("er_close", device)
    bind("er_get_info", device, result=ct.POINTER(Info))
    bind("er_get_identity", device, ct.POINTER(Identity))
    bind("er_get_capability", device, ct.c_int, ct.POINTER(CapabilityInfo))
    for name in ("brightness", "warmth"):
        bind("er_get_" + name, device, ct.POINTER(ct.c_int))
        bind("er_set_" + name, device, ct.c_int)
    for name in ("wifi", "cover"):
        bind("er_get_" + name, device, ct.POINTER(ct.c_bool))
    for name in ("wifi", "frontlight", "auto_brightness"):
        bind("er_set_" + name, device, ct.c_bool)
    for name, structure in (
        ("battery", Battery),
        ("usb", USB),
        ("display", Display),
        ("frontlight", Frontlight),
        ("wake_reason", Wake),
    ):
        bind("er_get_" + name, device, ct.POINTER(structure))
    bind("er_set_rotation", device, ct.c_uint)
    bind("er_poll_event", device, ct.POINTER(Event), ct.c_int)
    bind("er_sample_status", device)
    bind("er_set_low_battery_threshold", device, ct.c_int)
    bind("er_set_lifecycle_handler", device, LifecycleHandler, ct.c_void_p)
    bind("er_inhibit_suspend", device, ct.c_bool)
    for name in ("release_suspend", "cancel_wake_alarm", "suspend"):
        bind("er_" + name, device)
    for name in ("set_wake_alarm", "suspend_for"):
        bind("er_" + name, device, ct.c_uint)
    bind("er_renew_awake", device, ct.c_uint)
    bind("er_release_awake", device)
    bind("er_retain_display", device)
    bind("er_release_display", device)
    bind("er_get_last_sleep_ms", device, ct.POINTER(ct.c_uint64))
    bind("er_get_last_wake", device, ct.POINTER(Wake))
    bind("er_get_wifi_status", device, ct.POINTER(WifiStatus))
    bind(
        "er_wifi_list_networks",
        device,
        ct.POINTER(WifiNetwork),
        ct.c_size_t,
        ct.POINTER(ct.c_size_t),
    )
    bind(
        "er_wifi_scan",
        device,
        ct.POINTER(WifiAccessPoint),
        ct.c_size_t,
        ct.POINTER(ct.c_size_t),
    )
    bind("er_get_wifi_capabilities", device, ct.POINTER(WifiCapabilities))
    bind("er_wifi_add_network", device, ct.POINTER(WifiConfig), ct.c_int, ct.POINTER(WifiResult))
    bind(
        "er_wifi_update_network",
        device,
        ct.c_char_p,
        ct.POINTER(WifiConfig),
        ct.c_int,
        ct.POINTER(WifiResult),
    )
    bind("er_wifi_remove_network", device, ct.c_char_p, ct.c_int, ct.POINTER(WifiResult))
    for name in ("connect", "wait_connection"):
        bind("er_wifi_" + name, device, ct.c_char_p, ct.c_bool, ct.c_int, ct.POINTER(WifiResult))
    bind("er_wifi_disconnect", device, ct.c_int, ct.POINTER(WifiResult))
    bind("er_get_battery_details", device, ct.POINTER(BatteryDetails))
    bind("er_watch_capabilities", device, ct.c_uint32)
    bind("er_get_usb_gadget", device, ct.POINTER(USBGadget))
    bind("er_get_usb_function", device, ct.c_int, ct.POINTER(CapabilityInfo))
    bind("er_get_usb_combination", device, ct.c_uint32, ct.POINTER(CapabilityInfo))
    for name in ("apply_usb_config", "add_usb_functions"):
        bind("er_" + name, device, ct.POINTER(USBConfig), ct.POINTER(USBTransition))
    bind("er_remove_usb_functions", device, ct.c_uint32, ct.POINTER(USBTransition))
    bind("er_finish_usb_config", device, ct.POINTER(USBTransition))
    bind("er_prepare_usb", device, ct.POINTER(USBConfig))
    bind("er_activate_usb", device)
    bind("er_deactivate_usb", device)
    bind("er_release_usb", device)
    bind("er_configure_usb", device, ct.POINTER(USBConfig))
    bind("er_get_usb_mode", device, ct.POINTER(ct.c_uint32))
    bind("er_set_usb_mode", device, ct.c_uint32, ct.POINTER(USBModeTransition))
    bind("er_finish_usb_mode", device, ct.POINTER(USBModeTransition))
    return lib


def text(value):
    return value.decode("utf-8", errors="surrogateescape")


def _snapshot(record, value, **overrides):
    """Copy matching fields; callers supply enum, sentinel, and optional values."""
    values = {field.name: getattr(value, field.name) for field in fields(record)}
    values.update(overrides)
    return record(
        **{name: text(item) if isinstance(item, bytes) else item for name, item in values.items()}
    )


def device_info(value):
    return _snapshot(
        types.DeviceInfo,
        value,
        capabilities=types.Capability(value.capabilities),
        features=_snapshot(types.Features, value.features),
    )


def device_identity(value):
    return _snapshot(types.DeviceIdentity, value)


def capability_info(value):
    return _snapshot(
        types.CapabilityInfo, value, reason_code=types.CapabilityReason(value.reason_code)
    )


def battery(value):
    return types.Battery(value.percent, types.ChargeState(value.state))


def battery_details(value):
    missing = {
        field.name: None
        for index, field in enumerate(fields(types.BatteryDetails))
        if not value.present & (1 << index)
    }
    return _snapshot(types.BatteryDetails, value, **missing)


def display(value):
    return _snapshot(types.Display, value)


def frontlight(value):
    return _snapshot(types.Frontlight, value)


def usb(value):
    return _snapshot(types.USB, value, state=types.USBConnectionState(value.state))


def usb_gadget(value):
    return _snapshot(types.USBGadget, value, functions=types.USBFunction(value.functions))


def wake(value):
    return types.Wake(types.WakeReason(value.reason), value.irq)


def event(value):
    kind = types.EventType(value.type)
    data = value.data
    if kind == types.EventType.BUTTON:
        payload = types.ButtonInput(
            types.Button(data.button.button), types.Action(data.button.action)
        )
    elif kind == types.EventType.TOUCH:
        touch = data.touch
        payload = types.TouchInput(touch.id, touch.x, touch.y, types.Action(touch.action))
    elif kind == types.EventType.COVER:
        payload = data.cover_closed
    elif kind == types.EventType.ORIENTATION:
        payload = data.orientation
    elif kind in (types.EventType.BATTERY, types.EventType.LOW_BATTERY):
        payload = battery(data.battery)
    elif kind == types.EventType.USB:
        payload = usb(data.usb)
    elif kind == types.EventType.RESUMED:
        payload = wake(data.wake)
    elif kind in (types.EventType.SUSPEND_FAILED, types.EventType.STATUS_ERROR):
        payload = types.Failure(types.Capability(data.failure.capability), data.failure.error)
    elif kind == types.EventType.WIFI:
        payload = wifi_status(data.wifi)
    elif kind == types.EventType.CAPABILITY:
        payload = types.CapabilityChange(
            types.Capability(data.capability.capability), capability_info(data.capability.info)
        )
    else:
        payload = None
    return types.Event(kind, payload)


def signal(value):
    return None if value == -2147483648 else value


def wifi_network(value):
    return _snapshot(
        types.WifiNetwork,
        value,
        security=types.WifiSecurity(value.security),
        hidden=value.hidden if value.hidden_known else None,
        enabled=value.enabled if value.enabled_known else None,
    )


def wifi_access_point(value):
    return _snapshot(
        types.WifiAccessPoint,
        value,
        security=types.WifiSecurity(value.security),
        signal_dbm=signal(value.signal_dbm),
    )


def wifi_status(value):
    return _snapshot(
        types.WifiStatus,
        value,
        state=types.WifiState(value.state),
        signal_dbm=signal(value.signal_dbm),
    )


def wifi_capabilities(value):
    return _snapshot(
        types.WifiCapabilities,
        value,
        security_read=types.WifiSecurityMask(value.security_read),
        security_write=types.WifiSecurityMask(value.security_write),
    )


def wifi_result(value):
    return _snapshot(
        types.WifiResult,
        value,
        completion=types.WifiCompletion(value.completion),
        status=wifi_status(value.status) if value.status_known else None,
    )


def usb_transition(value):
    return _snapshot(
        types.USBTransition,
        value,
        requested=types.USBFunction(value.requested),
        active=types.USBFunction(value.active),
    )


def usb_mode_transition(value):
    return _snapshot(
        types.USBModeTransition,
        value,
        requested=types.USBMode(value.requested),
        active=types.USBMode(value.active),
        state=types.USBModeState(value.state),
    )
