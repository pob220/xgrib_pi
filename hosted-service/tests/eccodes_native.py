"""Small test-only ecCodes C binding for independent decoded-value checks."""
import ctypes as C
import ctypes.util
from pathlib import Path

lib = C.CDLL(ctypes.util.find_library("eccodes"))
signatures = {
    "codes_handle_new_from_samples": (C.c_void_p, [C.c_void_p, C.c_char_p]),
    "codes_handle_new_from_message_copy": (C.c_void_p, [C.c_void_p, C.c_void_p, C.c_size_t]),
    "codes_handle_delete": (C.c_int, [C.c_void_p]),
    "codes_get_error_message": (C.c_char_p, [C.c_int]),
    "codes_set_long": (C.c_int, [C.c_void_p, C.c_char_p, C.c_long]),
    "codes_set_double": (C.c_int, [C.c_void_p, C.c_char_p, C.c_double]),
    "codes_set_string": (C.c_int, [C.c_void_p, C.c_char_p, C.c_char_p, C.POINTER(C.c_size_t)]),
    "codes_get_long": (C.c_int, [C.c_void_p, C.c_char_p, C.POINTER(C.c_long)]),
    "codes_get_double": (C.c_int, [C.c_void_p, C.c_char_p, C.POINTER(C.c_double)]),
    "codes_get_string": (C.c_int, [C.c_void_p, C.c_char_p, C.c_char_p, C.POINTER(C.c_size_t)]),
    "codes_get_size": (C.c_int, [C.c_void_p, C.c_char_p, C.POINTER(C.c_size_t)]),
    "codes_get_double_array": (C.c_int, [C.c_void_p, C.c_char_p, C.POINTER(C.c_double), C.POINTER(C.c_size_t)]),
    "codes_set_double_array": (C.c_int, [C.c_void_p, C.c_char_p, C.POINTER(C.c_double), C.c_size_t]),
    "codes_get_message": (C.c_int, [C.c_void_p, C.POINTER(C.c_void_p), C.POINTER(C.c_size_t)]),
}
for name, (result, arguments) in signatures.items():
    function = getattr(lib, name)
    function.restype, function.argtypes = result, arguments


def check(error):
    if error:
        raise RuntimeError(lib.codes_get_error_message(error).decode())


class Handle:
    def __init__(self, data=None):
        self.pointer = (lib.codes_handle_new_from_samples(None, b"regular_ll_sfc_grib2") if data is None
                        else lib.codes_handle_new_from_message_copy(None, data, len(data)))
        if not self.pointer:
            raise RuntimeError("unable to decode sample/message")

    def __enter__(self):
        return self

    def __exit__(self, *_):
        lib.codes_handle_delete(self.pointer)

    def set(self, key, value):
        key = key.encode()
        if isinstance(value, str):
            n = C.c_size_t(len(value))
            check(lib.codes_set_string(self.pointer, key, value.encode(), C.byref(n)))
        elif isinstance(value, int):
            check(lib.codes_set_long(self.pointer, key, value))
        else:
            check(lib.codes_set_double(self.pointer, key, value))

    def long(self, key):
        value = C.c_long()
        check(lib.codes_get_long(self.pointer, key.encode(), C.byref(value)))
        return value.value

    def number(self, key):
        value = C.c_double()
        check(lib.codes_get_double(self.pointer, key.encode(), C.byref(value)))
        return value.value

    def string(self, key):
        value = C.create_string_buffer(128)
        n = C.c_size_t(128)
        check(lib.codes_get_string(self.pointer, key.encode(), value, C.byref(n)))
        return value.value.decode()

    def array(self, key="values"):
        n = C.c_size_t()
        check(lib.codes_get_size(self.pointer, key.encode(), C.byref(n)))
        values = (C.c_double * n.value)()
        check(lib.codes_get_double_array(self.pointer, key.encode(), values, C.byref(n)))
        return list(values)

    def values(self, values):
        array = (C.c_double * len(values))(*values)
        check(lib.codes_set_double_array(self.pointer, b"values", array, len(values)))

    def data(self):
        pointer, n = C.c_void_p(), C.c_size_t()
        check(lib.codes_get_message(self.pointer, C.byref(pointer), C.byref(n)))
        return C.string_at(pointer, n.value)


def messages(path):
    data = Path(path).read_bytes()
    offset = 0
    while offset < len(data):
        assert data[offset:offset+4] == b"GRIB"
        edition = data[offset+7]
        assert edition in (1, 2)
        n = int.from_bytes(data[offset+8:offset+16] if edition == 2 else data[offset+4:offset+7], "big")
        assert n >= 20 and offset+n <= len(data)
        yield data[offset:offset+n]
        offset += n


def decoded(path):
    output = []
    for data in messages(path):
        with Handle(data) as h:
            output.append({"name": h.string("shortName"), "units": h.string("units"),
                           "cycle": (h.long("dataDate"), h.long("dataTime")),
                           "step": h.long("endStep"), "level": h.number("level"),
                           "typeOfLevel": h.string("typeOfLevel"),
                           "ni": h.long("Ni"), "nj": h.long("Nj"),
                           "lat": h.array("latitudes"), "lon": h.array("longitudes"),
                           "values": h.array(), "missing": h.number("missingValue"),
                           "quantum": 2**h.long("binaryScaleFactor") * 10**(-h.long("decimalScaleFactor"))})
    return output
