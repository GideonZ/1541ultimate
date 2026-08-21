#!/usr/bin/env python3
"""The parts of the document that are not about any single endpoint.

Everything an endpoint says about itself lives in the API_DOC block beside it.
What is left is here: the product families, the prose that frames the document,
the tag list, the security scheme and the response schemas that endpoints share.
A schema an API_DOC names but this module does not define is a build failure.
"""

# `defines` and `targets` are the two things that decide which calls a product
# serves: the macros its sources are compiled with, and which route sources its
# makefiles compile. Both are read from the tree rather than restated here.
PROFILES = {
    "u2": {
        "defines": {},
        "targets": (
            "target/u2/riscv/ultimate/Makefile",
            "target/u2plus/nios/ultimate/Makefile",
            "target/u2plus_L/riscv/ultimate/Makefile",
        ),
        "title": "Ultimate II REST API",
        "products": "the Ultimate II, Ultimate II+ and Ultimate II+L",
        "default_host": "ultimate",
    },
    "u64": {
        "defines": {"U64": 1},
        "targets": (
            "target/u64/nios2/ultimate/Makefile",
            "target/u64ii/riscv/ultimate/Makefile",
        ),
        "title": "Ultimate 64 REST API",
        "products": "the Ultimate 64, Ultimate 64 Elite and Ultimate 64 Elite II",
        "default_host": "ultimate64",
    },
}

API_VERSION = "0.1"

DESCRIPTION = """\
The HTTP interface of the Ultimate firmware, available since release 3.11.

It covers {products}. There is a separate document for the other family,
because which calls a product serves is decided when the firmware is compiled
rather than at run time.

This document is generated from the firmware sources. Every call in it is
registered in `software/api/route_*.cc` in the build it came from, and no call
registered there is missing from it, so a generated client cannot offer a method
that these products do not have.

## Calling convention

A request is addressed as `/v1/<route>/<path>:<command>?<arguments>`. The verb
selects the kind of operation: `GET` reads, `PUT` acts on arguments carried in
the URL, and `POST` acts on data carried in the request body.

## Responses

Unless a call returns a binary attachment, the response is
`Content-Type: application/json` and carries an `errors` array. The array is
empty when the call succeeded. A call that returns an attachment answers with
`Content-Type: application/octet-stream` and a `Content-Disposition` header, or
with `203 No Content` when there is nothing to return.

## Authentication

From release 3.12 a network password can be configured on the device. While one
is set, every request must carry it in an `X-Password` header. A request without
the header, or with the wrong value, is answered with `403 Forbidden`. While no
password is configured the header is ignored.
"""

TAGS = [
    ("About", "Firmware, FPGA and product identification."),
    ("Configuration", "Read and write the settings the menu also exposes."),
    ("Machine", "Reset, pause, memory access and the Ultimate menu."),
    ("Input", "Keyboard and joystick injection."),
    ("Drives", "The emulated disk drives and the images mounted in them."),
    ("Runners", "Start a program, cartridge or tune on the machine."),
    ("Files", "Inspect files and create empty disk images."),
    ("Streams", "Video, audio and debug streams sent to a listening host."),
    ("Diagnostics", "Bus timing capture and heap accounting."),
]

SECURITY_SCHEMES = {
    "NetworkPassword": {
        "type": "apiKey",
        "in": "header",
        "name": "X-Password",
        "description": (
            "The network password configured on the device, sent on every request. "
            "Required only while a password is configured; ignored otherwise."
        ),
    }
}

SCHEMAS = {
    "ErrorResponse": {
        "type": "object",
        "description": (
            "Present on every JSON response. The array is empty when the call succeeded."
        ),
        "required": ["errors"],
        "properties": {
            "errors": {
                "type": "array",
                "description": "One entry per problem the firmware reported.",
                "items": {"type": "string"},
            }
        },
    },
    "VersionResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "required": ["version"],
                "properties": {
                    "version": {
                        "type": "string",
                        "description": "Version of the REST interface itself, not of the firmware.",
                        "examples": [API_VERSION],
                    }
                },
            },
        ]
    },
    "InfoResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "properties": {
                    "product": {
                        "type": "string",
                        "description": "Full product name, including the core version on Ultimate 64 hardware.",
                    },
                    "firmware_version": {"type": "string"},
                    "fpga_version": {
                        "type": "string",
                        "description": "FPGA build number in hexadecimal.",
                    },
                    "core_version": {
                        "type": "string",
                        "description": "C64 core version. Ultimate 64 hardware only.",
                    },
                    "hostname": {"type": "string"},
                    "unique_id": {
                        "type": "string",
                        "description": "Omitted when the device has no unique id configured.",
                    },
                },
            },
        ]
    },
    "HeapResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "properties": {
                    "free": {
                        "type": "integer",
                        "description": "Bytes free in the FreeRTOS heap right now. Diff two samples to find a leak.",
                    },
                    "min_ever_free": {
                        "type": "integer",
                        "description": (
                            "Low water mark since boot. It never recovers, so it shows headroom "
                            "but cannot distinguish a leak from a transient peak."
                        ),
                    },
                    "total": {"type": "integer", "description": "Size of the heap."},
                },
            },
        ]
    },
    "DebugRegisterResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "required": ["value"],
                "properties": {
                    "value": {
                        "type": "string",
                        "description": "Contents of the debug register at $D7FF, two hexadecimal digits.",
                        "examples": ["1F"],
                    }
                },
            },
        ]
    },
    "MemoryWriteResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "properties": {
                    "address": {
                        "type": "string",
                        "description": "Range that was written, as first-last in hexadecimal.",
                        "examples": ["d020-d021"],
                    }
                },
            },
        ]
    },
    "ConfigCategoriesResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "properties": {
                    "categories": {
                        "type": "array",
                        "description": "Name of every configuration category on this device.",
                        "items": {"type": "string"},
                    }
                },
            },
        ]
    },
    "ConfigValuesResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "description": (
                    "One property per matching category, each holding one property per "
                    "matching item whose value is the current setting."
                ),
                "additionalProperties": {
                    "type": "object",
                    "additionalProperties": {
                        "oneOf": [{"type": "string"}, {"type": "integer"}]
                    },
                },
            },
        ]
    },
    "ConfigItemsResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "description": (
                    "One property per matching category, each holding one property per "
                    "matching item described in full."
                ),
                "additionalProperties": {
                    "type": "object",
                    "additionalProperties": {"$ref": "#/components/schemas/ConfigItem"},
                },
            },
        ]
    },
    "ConfigItem": {
        "type": "object",
        "description": (
            "A single setting. Which of the optional members are present depends on the "
            "type of the setting: an enumeration carries values, a numeric setting carries "
            "min, max and format, and a string setting with suggestions carries presets."
        ),
        "required": ["current"],
        "properties": {
            "current": {
                "oneOf": [{"type": "string"}, {"type": "integer"}],
                "description": "Value in effect.",
            },
            "default": {
                "oneOf": [{"type": "string"}, {"type": "integer"}],
                "description": "Value the setting reverts to on reset to default.",
            },
            "values": {
                "type": "array",
                "description": "Accepted values of an enumeration setting.",
                "items": {"type": "string"},
            },
            "presets": {
                "type": "array",
                "description": "Suggested values of a string setting. Other values are accepted.",
                "items": {"type": "string"},
            },
            "min": {"type": "integer", "description": "Lowest accepted value of a numeric setting."},
            "max": {"type": "integer", "description": "Highest accepted value of a numeric setting."},
            "format": {
                "type": "string",
                "description": "printf style format the menu renders a numeric setting with.",
            },
        },
    },
    "ConfigStoreListResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "description": (
                    "Names of the categories the call acted on. The member is present only "
                    "when the request selected categories by path."
                ),
                "properties": {
                    "loaded": {"type": "array", "items": {"type": "string"}},
                    "written": {"type": "array", "items": {"type": "string"}},
                    "reset": {"type": "array", "items": {"type": "string"}},
                },
            },
        ]
    },
    "FileUpload": {
        "type": "object",
        "description": (
            "A multipart form with the payload as a file part. The firmware takes the "
            "name from the part, or from the Content-Disposition header when the body is "
            "sent raw instead."
        ),
        "properties": {
            "file": {"type": "string", "format": "binary"},
        },
    },
    "ConfigUpdate": {
        "type": "object",
        "description": (
            "One property per category to update, each holding one property per item to "
            "set. Categories and items are matched by exact name."
        ),
        "additionalProperties": {
            "type": "object",
            "additionalProperties": {"oneOf": [{"type": "string"}, {"type": "integer"}]},
        },
    },
    "DrivesResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "properties": {
                    "drives": {
                        "type": "array",
                        "description": (
                            "One entry per drive present in this build. Each entry has a single "
                            "property named after the drive, `a` and `b` for the emulated drives "
                            "and `softiec` for the IEC file system."
                        ),
                        "items": {
                            "type": "object",
                            "additionalProperties": {"$ref": "#/components/schemas/DriveInfo"},
                        },
                    }
                },
            },
        ]
    },
    "DriveInfo": {
        "type": "object",
        "properties": {
            "enabled": {"type": "boolean", "description": "Whether the drive is powered."},
            "bus_id": {"type": "integer", "description": "IEC device number, normally 8 to 11."},
            "type": {
                "type": "string",
                "description": "Emulated drive model.",
                "examples": ["1541"],
            },
            "rom": {"type": "string", "description": "ROM image the drive is running."},
            "image_file": {"type": "string", "description": "File name of the mounted image, empty when none."},
            "image_path": {"type": "string", "description": "Directory the mounted image was loaded from."},
            "last_error": {"type": "string", "description": "Last IEC error, softiec only."},
            "partitions": {
                "type": "array",
                "description": "Partitions exposed by the IEC file system, softiec only.",
                "items": {
                    "type": "object",
                    "properties": {
                        "id": {"type": "integer"},
                        "path": {"type": "string"},
                    },
                },
            },
        },
    },
    "DriveMountResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "description": "Echoes how the request was interpreted, which is useful when a mount is rejected.",
                "properties": {
                    "Subsys": {"type": "integer", "description": "Subsystem the command was sent to."},
                    "Ftype": {"type": "integer", "description": "Drive model implied by the image type."},
                    "command": {"type": "integer", "description": "Mount command including the mode flags."},
                    "file": {"type": "string", "description": "Image the drive was told to mount."},
                },
            },
        ]
    },
    "DriveModeResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "properties": {
                    "mode": {"type": "string", "description": "Drive model that was selected.", "examples": ["1571"]}
                },
            },
        ]
    },
    "FileInfoResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "properties": {
                    "files": {
                        "type": "object",
                        "properties": {
                            "path": {"type": "string", "description": "Path as the request spelled it."},
                            "filename": {"type": "string", "description": "Long file name as stored."},
                            "size": {"type": "integer", "description": "Size in bytes."},
                            "extension": {"type": "string", "description": "Extension, upper case, without a dot."},
                        },
                    }
                },
            },
        ]
    },
    "DiskImageResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "properties": {
                    "path": {"type": "string", "description": "File that was created."},
                    "tracks": {"type": "integer", "description": "Track count the image was formatted with."},
                    "diskname": {"type": "string", "description": "Disk name written into the directory header."},
                    "bytes_written": {
                        "type": "integer",
                        "description": "Bytes the firmware zeroed before formatting.",
                    },
                },
            },
        ]
    },
    "InputStateResponse": {
        "allOf": [
            {"$ref": "#/components/schemas/ErrorResponse"},
            {
                "type": "object",
                "properties": {
                    "keyboard": {
                        "type": "object",
                        "properties": {
                            "inputs": {
                                "type": "array",
                                "description": "Every key currently held, by the same names the request uses.",
                                "items": {"type": "string"},
                            }
                        },
                    },
                    "joysticks": {
                        "type": "array",
                        "description": "One entry per port, always both ports, in order.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "port": {"type": "integer", "enum": [1, 2]},
                                "inputs": {
                                    "type": "array",
                                    "description": "Every direction or button currently held.",
                                    "items": {"type": "string"},
                                },
                            },
                        },
                    },
                },
            },
        ]
    },
    "InputBatch": {
        "type": "object",
        "description": (
            "A batch of input events, applied in order. The whole batch is validated "
            "before any of it is applied, so a rejected batch changes nothing."
        ),
        "required": ["events"],
        "properties": {
            "events": {
                "type": "array",
                "minItems": 1,
                "maxItems": 64,
                "items": {"$ref": "#/components/schemas/InputEvent"},
            }
        },
    },
    "InputEvent": {
        "oneOf": [
            {"$ref": "#/components/schemas/KeyboardEvent"},
            {"$ref": "#/components/schemas/JoystickEvent"},
            {"$ref": "#/components/schemas/ReleaseAllEvent"},
        ]
    },
    "KeyboardEvent": {
        "type": "object",
        "description": (
            "Keys to press, release or tap. `restore` is not part of the matrix and must "
            "appear on its own with transition `tap`."
        ),
        "required": ["kind", "inputs", "transition"],
        "additionalProperties": False,
        "properties": {
            "kind": {"const": "keyboard"},
            "transition": {"$ref": "#/components/schemas/InputTransition"},
            "inputs": {
                "type": "array",
                "minItems": 1,
                "maxItems": 8,
                "description": "Key names, no duplicates.",
                "items": {"$ref": "#/components/schemas/KeyboardInput"},
            },
        },
    },
    "JoystickEvent": {
        "type": "object",
        "required": ["kind", "port", "inputs", "transition"],
        "additionalProperties": False,
        "properties": {
            "kind": {"const": "joystick"},
            "port": {"type": "integer", "enum": [1, 2], "description": "Control port to drive."},
            "transition": {"$ref": "#/components/schemas/InputTransition"},
            "inputs": {
                "type": "array",
                "minItems": 1,
                "maxItems": 7,
                "description": "Directions and buttons, no duplicates.",
                "items": {"$ref": "#/components/schemas/JoystickInput"},
            },
        },
    },
    "ReleaseAllEvent": {
        "type": "object",
        "description": "Releases every key and joystick input the API is holding. Takes no other members.",
        "required": ["kind"],
        "additionalProperties": False,
        "properties": {"kind": {"const": "release_all"}},
    },
    "InputTransition": {
        "type": "string",
        "enum": ["press", "release", "tap"],
        "description": (
            "`press` holds the input until something releases it, `release` lets it go, "
            "and `tap` presses and releases it for you."
        ),
    },
    "KeyboardInput": {
        "type": "string",
        "description": "A key of the C64 keyboard, plus `restore`, which is wired to NMI rather than to the matrix.",
        "enum": [
            "inst_del", "return", "cursor_left_right", "f7", "f1", "f3", "f5", "cursor_up_down",
            "3", "w", "a", "4", "z", "s", "e", "left_shift",
            "5", "r", "d", "6", "c", "f", "t", "x",
            "7", "y", "g", "8", "b", "h", "u", "v",
            "9", "i", "j", "0", "m", "k", "o", "n",
            "plus", "p", "l", "minus", "period", "colon", "at", "comma",
            "pound", "star", "semicolon", "clr_home", "right_shift", "equals", "arrow_up", "slash",
            "1", "arrow_left", "ctrl", "2", "space", "commodore", "q", "run_stop",
            "restore",
        ],
    },
    "JoystickInput": {
        "type": "string",
        "description": "A direction or button of a joystick. `fire2` and `fire3` need a pad that has them.",
        "enum": ["up", "down", "left", "right", "fire", "fire2", "fire3"],
    },
}
