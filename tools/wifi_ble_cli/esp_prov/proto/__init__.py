# SPDX-FileCopyrightText: 2018-2022 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#
# Standalone variant: load the generated _pb2 modules vendored alongside this
# package and register them in sys.modules under their bare top-level names,
# since each generated module cross-references the others via bare imports
# (e.g. `import constants_pb2`).

import importlib.util
import os
import sys
from importlib.abc import Loader
from typing import Any

_HERE = os.path.dirname(os.path.abspath(__file__))


def _load_source(name: str, path: str) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if not spec:
        return None

    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    assert isinstance(spec.loader, Loader)
    spec.loader.exec_module(module)
    return module


constants_pb2 = _load_source('constants_pb2', os.path.join(_HERE, 'constants_pb2.py'))
sec0_pb2      = _load_source('sec0_pb2',      os.path.join(_HERE, 'sec0_pb2.py'))
sec1_pb2      = _load_source('sec1_pb2',      os.path.join(_HERE, 'sec1_pb2.py'))
sec2_pb2      = _load_source('sec2_pb2',      os.path.join(_HERE, 'sec2_pb2.py'))
session_pb2   = _load_source('session_pb2',   os.path.join(_HERE, 'session_pb2.py'))

wifi_constants_pb2 = _load_source('wifi_constants_pb2', os.path.join(_HERE, 'wifi_constants_pb2.py'))
wifi_config_pb2    = _load_source('wifi_config_pb2',    os.path.join(_HERE, 'wifi_config_pb2.py'))
wifi_scan_pb2      = _load_source('wifi_scan_pb2',      os.path.join(_HERE, 'wifi_scan_pb2.py'))
wifi_ctrl_pb2      = _load_source('wifi_ctrl_pb2',      os.path.join(_HERE, 'wifi_ctrl_pb2.py'))
