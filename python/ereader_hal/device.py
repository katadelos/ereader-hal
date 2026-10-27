"""Python interface to the e-reader HAL shared library."""

import ctypes as ct
import errno
import operator
import os
import warnings
import weakref
from contextlib import contextmanager

from . import _native, types


def _check(result):
    if result < 0:
        raise OSError(-result, os.strerror(-result))
    return result


def _finalize(close, handle):
    result = close(handle)
    if result < 0:
        warnings.warn(
            f"HAL device cleanup failed: {os.strerror(-result)} ({-result})",
            ResourceWarning,
            stacklevel=2,
        )


class WifiOperationError(OSError):
    """A failed or uncertain Wi-Fi operation, with its last observed result."""

    def __init__(self, result):
        super().__init__(result.error, os.strerror(result.error))
        self.result = result


class USBTransitionError(OSError):
    """A USB transition failure, including cleanup and restoration results."""

    def __init__(self, transition):
        super().__init__(transition.error, os.strerror(transition.error))
        self.transition = transition


def _integer(value, minimum, maximum):
    value = operator.index(value)
    if not minimum <= value <= maximum:
        raise ValueError("value must be between {} and {}".format(minimum, maximum))
    return value


def models(library=None):
    """Return catalogue model names without opening a device."""
    lib = _native.load(library)
    names = []
    index = 0
    while True:
        name = lib.er_model_name(index)
        if name is None:
            return names
        names.append(name.decode())
        index += 1


def model_info(model, library=None):
    """Return static facts for a catalogue model without opening hardware."""
    lib = _native.load(library)
    index = 0
    while (name := lib.er_model_name(index)) is not None:
        if name.decode() == model:
            value = _native.Info()
            _check(lib.er_get_model_info(index, ct.byref(value)))
            return _native.device_info(value)
        index += 1
    raise ValueError(f"unknown model: {model}")


def capability_name(capability, library=None):
    capability = _capability(capability)
    return _native.load(library).er_capability_name(capability).decode()


def capability_reason_name(reason, library=None):
    reason = types.CapabilityReason(reason)
    return _native.load(library).er_capability_reason_name(reason).decode()


def _capability(value):
    value = operator.index(value)
    if value not in {int(member) for member in types.Capability}:
        raise ValueError("choose one Capability member")
    return value


class Device:
    """An open HAL handle. Use as a context manager and serialize calls to each handle."""

    def __init__(self, library=None):
        self._lib = _native.load(library)
        self._handle = ct.c_void_p()
        self._callback = None
        self._callback_error = None
        self._in_callback = False
        _check(self._lib.er_open(ct.byref(self._handle)))
        self._finalizer = weakref.finalize(self, _finalize, self._lib.er_close, self._handle)

    def _require_open(self):
        if self._in_callback:
            raise RuntimeError("do not call this device from its lifecycle handler")
        if not self._finalizer.alive:
            raise RuntimeError("device is closed")

    def _call(self, function, *arguments):
        self._require_open()
        result = function(self._handle, *arguments)
        if self._callback_error is not None:
            error = self._callback_error
            self._callback_error = None
            raise error
        return _check(result)

    def _get(self, function, structure):
        value = structure()
        self._call(function, ct.byref(value))
        return value

    def close(self):
        """Release the device. Cleanup failure keeps it open so close() can be retried."""
        if self._in_callback:
            raise RuntimeError("do not close this device from its lifecycle handler")
        if self._finalizer.alive:
            _check(self._lib.er_close(self._handle))
            self._finalizer.detach()
            self._handle = ct.c_void_p()
            self._callback = None

    def __enter__(self):
        self._require_open()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def get_info(self):
        """Return a snapshot that remains valid after closing the device."""
        self._require_open()
        return _native.device_info(self._lib.er_get_info(self._handle).contents)

    def get_identity(self):
        """Read serial and Wi-Fi identity; unavailable fields are empty strings."""
        return _native.device_identity(self._get(self._lib.er_get_identity, _native.Identity))

    def get_capability(self, capability):
        value = _native.CapabilityInfo()
        self._call(self._lib.er_get_capability, _capability(capability), ct.byref(value))
        return _native.capability_info(value)

    def get_brightness(self):
        return self._get(self._lib.er_get_brightness, ct.c_int).value

    def set_brightness(self, percent):
        self._call(self._lib.er_set_brightness, _integer(percent, 0, 100))

    def get_warmth(self):
        return self._get(self._lib.er_get_warmth, ct.c_int).value

    def set_warmth(self, percent):
        self._call(self._lib.er_set_warmth, _integer(percent, 0, 100))

    def get_frontlight(self):
        return _native.frontlight(self._get(self._lib.er_get_frontlight, _native.Frontlight))

    def set_frontlight(self, enabled):
        self._call(self._lib.er_set_frontlight, enabled)

    def set_auto_brightness(self, enabled):
        self._call(self._lib.er_set_auto_brightness, enabled)

    def get_wifi(self):
        return self._get(self._lib.er_get_wifi, ct.c_bool).value

    def set_wifi(self, enabled):
        self._call(self._lib.er_set_wifi, enabled)

    def get_battery(self):
        return _native.battery(self._get(self._lib.er_get_battery, _native.Battery))

    def get_usb(self):
        return _native.usb(self._get(self._lib.er_get_usb, _native.USB))

    def get_cover(self):
        return self._get(self._lib.er_get_cover, ct.c_bool).value

    def get_display(self):
        return _native.display(self._get(self._lib.er_get_display, _native.Display))

    def set_rotation(self, degrees):
        degrees = operator.index(degrees)
        if degrees not in (0, 90, 180, 270):
            raise ValueError("rotation must be 0, 90, 180 or 270")
        self._call(self._lib.er_set_rotation, degrees)

    def poll_event(self, timeout_ms=-1):
        """Read input or queued events; -1 waits indefinitely. Sample firmware status separately."""
        value = _native.Event()
        result = self._call(
            self._lib.er_poll_event, ct.byref(value), _integer(timeout_ms, -1, 0x7FFFFFFF)
        )
        return _native.event(value) if result else None

    def sample_status(self):
        """Synchronously collect firmware status and watched capabilities for poll_event()."""
        self._call(self._lib.er_sample_status)

    def set_low_battery_threshold(self, percent):
        self._call(self._lib.er_set_low_battery_threshold, _integer(percent, 0, 100))

    def set_lifecycle_handler(self, handler):
        """Set handler(Event), or None to clear it. Return None/0 or a negative errno to veto.

        Exceptions veto sleep with ECANCELED and are re-raised after the C call returns.
        """
        if handler is None:
            callback = _native.LifecycleHandler()
        else:
            if not callable(handler):
                raise TypeError("handler must be callable or None")

            def notify(event, context):
                self._in_callback = True
                try:
                    result = handler(_native.event(event.contents))
                    return 0 if result is None else _integer(result, -0x7FFFFFFF, 0)
                except BaseException as error:
                    if self._callback_error is None:
                        self._callback_error = error
                    return -errno.ECANCELED
                finally:
                    self._in_callback = False

            callback = _native.LifecycleHandler(notify)
        self._call(self._lib.er_set_lifecycle_handler, callback, None)
        self._callback = callback

    def get_wake_reason(self):
        return _native.wake(self._get(self._lib.er_get_wake_reason, _native.Wake))

    def inhibit_suspend(self, system_scope=False):
        self._call(self._lib.er_inhibit_suspend, system_scope)

    def release_suspend(self):
        self._call(self._lib.er_release_suspend)

    @contextmanager
    def suspend_inhibited(self, system_scope=False):
        """Hold a suspend inhibitor for the duration of the with block."""
        self.inhibit_suspend(system_scope)
        try:
            yield self
        finally:
            self.release_suspend()

    def set_wake_alarm(self, seconds):
        self._call(self._lib.er_set_wake_alarm, _integer(seconds, 1, 86400))

    def cancel_wake_alarm(self):
        self._call(self._lib.er_cancel_wake_alarm)

    def suspend(self):
        self._call(self._lib.er_suspend)

    def suspend_for(self, seconds):
        self._call(self._lib.er_suspend_for, _integer(seconds, 1, 86400))

    def renew_awake(self, seconds=45):
        """Renew this handle's timed awake lease; call again before it expires."""
        self._call(self._lib.er_renew_awake, _integer(seconds, 1, 86400))

    def release_awake(self):
        self._call(self._lib.er_release_awake)

    def retain_display(self):
        """Hold stock screensaver policy until release/close; does not block suspend."""
        self._call(self._lib.er_retain_display)

    def release_display(self):
        """Restore the screensaver policy saved by this handle."""
        self._call(self._lib.er_release_display)

    def get_last_sleep_ms(self):
        """Elapsed milliseconds in the last confirmed suspend cycle, or zero if refused."""
        return self._get(self._lib.er_get_last_sleep_ms, ct.c_uint64).value

    def get_last_wake(self):
        """Return the wake source observed around the last HAL suspend call."""
        return _native.wake(self._get(self._lib.er_get_last_wake, _native.Wake))

    def get_wifi_status(self):
        return _native.wifi_status(self._get(self._lib.er_get_wifi_status, _native.WifiStatus))

    def _network_list(self, function, structure, convert):
        capacity = 64
        while True:
            values = (structure * capacity)()
            count = ct.c_size_t()
            try:
                self._call(function, values, capacity, ct.byref(count))
            except OSError as error:
                if error.errno != errno.ENOSPC or count.value <= capacity:
                    raise
                capacity = count.value
                continue
            return [convert(values[index]) for index in range(count.value)]

    def wifi_list_networks(self):
        return self._network_list(
            self._lib.er_wifi_list_networks, _native.WifiNetwork, _native.wifi_network
        )

    def wifi_scan(self):
        return self._network_list(
            self._lib.er_wifi_scan, _native.WifiAccessPoint, _native.wifi_access_point
        )

    @staticmethod
    def _network_config(config):
        ssid = Device._network_text(config.ssid, 32)
        password = None
        if config.password is not None:
            password = config.password.encode("utf-8", errors="surrogateescape")
            if b"\0" in password:
                raise ValueError("password cannot contain a null byte")
        return _native.WifiConfig(
            ssid, types.WifiSecurity(config.security), config.hidden, config.enabled, password
        )

    @staticmethod
    def _network_text(value, maximum=127):
        encoded = value.encode("utf-8", errors="surrogateescape")
        if not encoded or len(encoded) > maximum or b"\0" in encoded:
            raise ValueError("network text is empty, too long or contains a null byte")
        return encoded

    def get_wifi_capabilities(self):
        """Return supported security modes and profile editing operations."""
        return _native.wifi_capabilities(
            self._get(self._lib.er_get_wifi_capabilities, _native.WifiCapabilities)
        )

    def _wifi_operation(self, function, *arguments, timeout_ms):
        result = _native.WifiResult()
        try:
            self._call(function, *arguments, _integer(timeout_ms, 0, 0x7FFFFFFF), ct.byref(result))
        except OSError as error:
            raise WifiOperationError(_native.wifi_result(result)) from error
        return _native.wifi_result(result)

    def wifi_add_network(self, config, timeout_ms=5000):
        """Persist a network and return WifiResult containing its opaque network_id."""
        native = self._network_config(config)
        return self._wifi_operation(
            self._lib.er_wifi_add_network, ct.byref(native), timeout_ms=timeout_ms
        )

    def wifi_update_network(self, identifier, config, timeout_ms=5000):
        native = self._network_config(config)
        return self._wifi_operation(
            self._lib.er_wifi_update_network,
            self._network_text(identifier),
            ct.byref(native),
            timeout_ms=timeout_ms,
        )

    def wifi_remove_network(self, identifier, timeout_ms=5000):
        return self._wifi_operation(
            self._lib.er_wifi_remove_network, self._network_text(identifier), timeout_ms=timeout_ms
        )

    def wifi_connect(self, identifier, wait_for_address=True, timeout_ms=30000):
        """Wait for association and, by default, an IP address."""
        return self._wifi_operation(
            self._lib.er_wifi_connect,
            self._network_text(identifier),
            wait_for_address,
            timeout_ms=timeout_ms,
        )

    def wifi_disconnect(self, timeout_ms=5000):
        return self._wifi_operation(self._lib.er_wifi_disconnect, timeout_ms=timeout_ms)

    def wifi_wait_connection(self, identifier, wait_for_address=True, timeout_ms=30000):
        """Observe completion without replaying a request. None waits for disconnection."""
        return self._wifi_operation(
            self._lib.er_wifi_wait_connection,
            None if identifier is None else self._network_text(identifier),
            wait_for_address,
            timeout_ms=timeout_ms,
        )

    def get_battery_details(self):
        return _native.battery_details(
            self._get(self._lib.er_get_battery_details, _native.BatteryDetails)
        )

    def watch_capabilities(self, capabilities=types.Capability(0)):
        """Enable initial snapshots and change events during sample_status()."""
        mask = sum(int(capability) for capability in types.Capability)
        self._call(self._lib.er_watch_capabilities, _integer(capabilities, 0, mask))

    def get_usb_gadget(self):
        return _native.usb_gadget(self._get(self._lib.er_get_usb_gadget, _native.USBGadget))

    def get_usb_function(self, function):
        """Probe one USB function using the same capability fields as other controls."""
        function = operator.index(function)
        if function not in {int(member) for member in types.USBFunction}:
            raise ValueError("choose one USBFunction member")
        value = _native.CapabilityInfo()
        self._call(self._lib.er_get_usb_function, function, ct.byref(value))
        return _native.capability_info(value)

    @staticmethod
    def _usb_config(config, additions=False, template=False):
        def text(value):
            if value is None:
                return None
            encoded = os.fsencode(value)
            if b"\0" in encoded:
                raise ValueError("USB strings cannot contain a null byte")
            return encoded

        descriptor = (ct.c_uint8 * len(config.hid_descriptor)).from_buffer_copy(
            config.hid_descriptor
        )
        return _native.USBConfig(
            _integer(config.functions, 0 if template else 1, (1 << 6) - 1),
            _integer(config.vendor_id, 0 if additions else 1, 65535),
            _integer(config.product_id, 0 if additions else 1, 65535),
            text(config.manufacturer),
            text(config.product),
            text(config.serial),
            text(config.device_mac),
            text(config.host_mac),
            descriptor,
            len(descriptor),
            _integer(config.hid_report_length, 0, 65535),
            _integer(config.hid_protocol, 0, 255),
            _integer(config.hid_subclass, 0, 255),
            config.take_stock_usb,
        )

    def prepare_usb(self, config):
        """Prepare an unbound gadget. For ADB, start adbd before activate_usb()."""
        native = self._usb_config(config)
        self._call(self._lib.er_prepare_usb, ct.byref(native))

    def get_usb_combination(self, functions):
        """Probe a function combination; the controller validates endpoint capacity on binding."""
        value = _native.CapabilityInfo()
        self._call(
            self._lib.er_get_usb_combination, _integer(functions, 1, (1 << 6) - 1), ct.byref(value)
        )
        return _native.capability_info(value)

    def _usb_transition(self, function, *arguments):
        transition = _native.USBTransition()
        try:
            self._call(function, *arguments, ct.byref(transition))
        except OSError as error:
            raise USBTransitionError(_native.usb_transition(transition)) from error
        return _native.usb_transition(transition)

    def apply_usb_config(self, config):
        """Replace the configuration, or release with None. Inspect prepared for ADB setup."""
        native = None if config is None else self._usb_config(config)
        return self._usb_transition(
            self._lib.er_apply_usb_config, None if native is None else ct.byref(native)
        )

    def add_usb_functions(self, additions):
        """Add functions with their settings. Zero IDs and None strings preserve identity."""
        native = self._usb_config(additions, additions=True)
        return self._usb_transition(self._lib.er_add_usb_functions, ct.byref(native))

    def remove_usb_functions(self, functions):
        return self._usb_transition(
            self._lib.er_remove_usb_functions, _integer(functions, 1, (1 << 6) - 1)
        )

    def finish_usb_config(self):
        """Activate after starting adbd, or retry pending cleanup and restoration."""
        return self._usb_transition(self._lib.er_finish_usb_config)

    def activate_usb(self):
        self._call(self._lib.er_activate_usb)

    def deactivate_usb(self):
        """Unbind while keeping functions prepared. Call before stopping adbd."""
        self._call(self._lib.er_deactivate_usb)

    def release_usb(self):
        """Release this handle's claim and restore the previous firmware USB policy.

        Stock storage follows normal USB events. For ADB, deactivate and stop adbd first.
        """
        self._call(self._lib.er_release_usb)

    def configure_usb(self, config):
        """Save a gadget template, or select native Kindle NETWORK with None.

        Identity and HID settings are reused across set_usb_mode() calls.
        config.functions selects ECM/RNDIS for NETWORK; the default is ECM.
        """
        native = None if config is None else self._usb_config(config, template=True)
        self._call(self._lib.er_configure_usb, None if native is None else ct.byref(native))

    def get_usb_mode(self):
        """Return the active gadget's USBMode flags, or zero while unbound."""
        return types.USBMode(self._get(self._lib.er_get_usb_mode, ct.c_uint32).value)

    def _usb_mode_transition(self, function, *arguments):
        transition = _native.USBModeTransition()
        try:
            self._call(function, *arguments, ct.byref(transition))
        except OSError as error:
            raise USBTransitionError(_native.usb_mode_transition(transition)) from error
        return _native.usb_mode_transition(transition)

    def set_usb_mode(self, mode):
        """Claim a mode until release/close; OR flags for a composite gadget.

        Native Kindle NETWORK needs no template. Custom gadgets use configure_usb().
        ADB can return PENDING until adbd opens its endpoints. FILESTORAGE is status-only.
        """
        return self._usb_mode_transition(self._lib.er_set_usb_mode, _integer(mode, 1, (1 << 5) - 1))

    def finish_usb_mode(self):
        """Finish pending ADB activation or retry a failed mode's restoration."""
        return self._usb_mode_transition(self._lib.er_finish_usb_mode)
