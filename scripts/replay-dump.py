#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Dump the contents of a recorded execution stream
#
#  Copyright (c) 2017 Alex Bennée <alex.bennee@linaro.org>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.

import argparse
import struct
import os
import sys
from collections import namedtuple
from os import path

# This mirrors some of the global replay state which some of the
# stream loading refers to. Some decoders may read the next event so
# we need handle that case. Calling reuse_event will ensure the next
# event is read from the cache rather than advancing the file.

class ReplayState(object):
    def __init__(self):
        self.event = -1
        self.event_count = 0
        self.already_read = False
        self.current_checkpoint = 0
        self.checkpoint = 0

    def set_event(self, ev):
        self.event = ev
        self.event_count += 1

    def get_event(self):
        self.already_read = False
        return self.event

    def reuse_event(self, ev):
        self.event = ev
        self.already_read = True

    def set_checkpoint(self):
        self.checkpoint = self.event - self.checkpoint_start

    def get_checkpoint(self):
        return self.checkpoint

replay_state = ReplayState()

# Simple read functions that mirror replay-internal.c
# The file-stream is big-endian and manually written out a byte at a time.

def read_byte(fin):
    "Read a single byte"
    return struct.unpack('>B', fin.read(1))[0]

def read_event(fin):
    "Read a single byte event, but save some state"
    if replay_state.already_read:
        return replay_state.get_event()
    else:
        replay_state.set_event(read_byte(fin))
        return replay_state.event

def read_word(fin):
    "Read a 16 bit word"
    return struct.unpack('>H', fin.read(2))[0]

def read_dword(fin):
    "Read a 32 bit word"
    return struct.unpack('>I', fin.read(4))[0]

def read_qword(fin):
    "Read a 64 bit word"
    return struct.unpack('>Q', fin.read(8))[0]

def read_array(fin):
    "Read a sized array"
    size = read_dword(fin)
    data = fin.read(size)
    return data

# Generic decoder structure
Decoder = namedtuple("Decoder", "eid name fn")

def call_decode(table, index, dumpfile):
    "Search decode table for next step"
    decoder = next((d for d in table if d.eid == index), None)
    if not decoder:
        print("Could not decode index: %d" % (index))
        print("Entry is: %s" % (decoder))
        print("Decode Table is:\n%s" % (table))
        raise(Exception("unknown event"))
    else:
        return decoder.fn(decoder.eid, decoder.name, dumpfile)

# Print event
def print_event(eid, name, string=None, event_count=None):
    "Print event with count"
    if not event_count:
        event_count = replay_state.event_count

    if string:
        print("%d:%s(%d) %s" % (event_count, name, eid, string))
    else:
        print("%d:%s(%d)" % (event_count, name, eid))


# Decoders for each event type

def decode_unimp(eid, name, _unused_dumpfile):
    "Unimplemented decoder, will trigger exit"
    print("%s not handled - will now stop" % (name))
    raise(Exception("unhandled event"))

def decode_plain(eid, name, _unused_dumpfile):
    "Plain events without additional data"
    print_event(eid, name, "no data")
    return True

# Checkpoint decoder
def swallow_async_qword(eid, name, dumpfile):
    "Swallow a qword of data without looking at it"
    step_id = read_qword(dumpfile)
    print("  %s(%d) @ %d" % (name, eid, step_id))
    return True

def swallow_bytes(eid, name, dumpfile, nr):
    """Swallow nr bytes of data without looking at it"""
    dumpfile.seek(nr, os.SEEK_CUR)

total_insns = 0

def decode_instruction(eid, name, dumpfile):
    global total_insns
    ins_diff = read_dword(dumpfile)
    total_insns += ins_diff
    print_event(eid, name, "+ %d -> %d" % (ins_diff, total_insns))
    return True

def decode_interrupt(eid, name, dumpfile):
    print_event(eid, name)
    return True

def decode_exception(eid, name, dumpfile):
    print_event(eid, name)
    return True

# v12 does away with the additional event byte and encodes it in the main type
# Between v8 and v9, REPLAY_ASYNC_BH_ONESHOT was added, but we don't decode
# those versions so leave it out.
async_decode_table = [ Decoder(0, "REPLAY_ASYNC_EVENT_BH", swallow_async_qword),
                       Decoder(1, "REPLAY_ASYNC_INPUT", decode_unimp),
                       Decoder(2, "REPLAY_ASYNC_INPUT_SYNC", decode_unimp),
                       Decoder(3, "REPLAY_ASYNC_CHAR_READ", decode_unimp),
                       Decoder(4, "REPLAY_ASYNC_EVENT_BLOCK", decode_unimp),
                       Decoder(5, "REPLAY_ASYNC_EVENT_NET", decode_unimp),
]
# See replay_read_events/replay_read_event
def decode_async_old(eid, name, dumpfile):
    """Decode an ASYNC event (pre-v8)"""

    print_event(eid, name)

    async_event_kind = read_byte(dumpfile)
    async_event_checkpoint = read_byte(dumpfile)

    if async_event_checkpoint != replay_state.current_checkpoint:
        print("  mismatch between checkpoint %d and async data %d" % (
            replay_state.current_checkpoint, async_event_checkpoint))
        return True

    return call_decode(async_decode_table, async_event_kind, dumpfile)

def decode_async_bh(eid, name, dumpfile):
    op_id = read_qword(dumpfile)
    print_event(eid, name)
    return True

def decode_async_bh_oneshot(eid, name, dumpfile):
    op_id = read_qword(dumpfile)
    print_event(eid, name)
    return True

def decode_async_char_read(eid, name, dumpfile):
    char_id = read_byte(dumpfile)
    size = read_dword(dumpfile)
    print_event(eid, name, "device:%x chars:%s" % (char_id, dumpfile.read(size)))
    return True

def decode_async_block(eid, name, dumpfile):
    op_id = read_qword(dumpfile)
    print_event(eid, name)
    return True

def decode_async_net(eid, name, dumpfile):
    net_id = read_byte(dumpfile)
    flags = read_dword(dumpfile)
    size = read_dword(dumpfile)
    swallow_bytes(eid, name, dumpfile, size)
    print_event(eid, name, "net:%x flags:%x bytes:%d" % (net_id, flags, size))
    return True

def decode_async_input(eid, name, dumpfile):
    typData = {
        "InputAxis": {
            "size": 4,
            "base": "unsigned int",
            "constants": {
                "INPUT_AXIS_X": 0,
                "INPUT_AXIS_Y": 1,
                "INPUT_AXIS__MAX": 2
            }
        },
        "InputButton": {
            "size": 4,
            "base": "unsigned int",
            "constants": {
                "INPUT_BUTTON_LEFT": 0,
                "INPUT_BUTTON_MIDDLE": 1,
                "INPUT_BUTTON_RIGHT": 2,
                "INPUT_BUTTON_WHEEL_UP": 3,
                "INPUT_BUTTON_WHEEL_DOWN": 4,
                "INPUT_BUTTON_SIDE": 5,
                "INPUT_BUTTON_EXTRA": 6,
                "INPUT_BUTTON_WHEEL_LEFT": 7,
                "INPUT_BUTTON_WHEEL_RIGHT": 8,
                "INPUT_BUTTON_TOUCH": 9,
                "INPUT_BUTTON__MAX": 10
            }
        },
        "InputEventKind": {
            "size": 4,
            "base": "unsigned int",
            "constants": {
                "INPUT_EVENT_KIND_KEY": 0,
                "INPUT_EVENT_KIND_BTN": 1,
                "INPUT_EVENT_KIND_REL": 2,
                "INPUT_EVENT_KIND_ABS": 3,
                "INPUT_EVENT_KIND_MTT": 4,
                "INPUT_EVENT_KIND__MAX": 5
            }
        },
        "InputMultiTouchType": {
            "size": 4,
            "base": "unsigned int",
            "constants": {
                "INPUT_MULTI_TOUCH_TYPE_BEGIN": 0,
                "INPUT_MULTI_TOUCH_TYPE_UPDATE": 1,
                "INPUT_MULTI_TOUCH_TYPE_END": 2,
                "INPUT_MULTI_TOUCH_TYPE_CANCEL": 3,
                "INPUT_MULTI_TOUCH_TYPE_DATA": 4,
                "INPUT_MULTI_TOUCH_TYPE__MAX": 5
            }
        },
        "KeyValueKind": {
            "size": 4,
            "base": "unsigned int",
            "constants": {
                "KEY_VALUE_KIND_NUMBER": 0,
                "KEY_VALUE_KIND_QCODE": 1,
                "KEY_VALUE_KIND__MAX": 2
            }
        },
        "QKbdModifier": {
            "size": 4,
            "base": "unsigned int",
            "constants": {
                "QKBD_MOD_NONE": 0,
                "QKBD_MOD_SHIFT": 1,
                "QKBD_MOD_CTRL": 2,
                "QKBD_MOD_ALT": 3,
                "QKBD_MOD_ALTGR": 4,
                "QKBD_MOD_NUMLOCK": 5,
                "QKBD_MOD_CAPSLOCK": 6,
                "QKBD_MOD__MAX": 7
            }
        },
        "QKeyCode": {
            "size": 4,
            "base": "unsigned int",
            "constants": {
                "Q_KEY_CODE_UNMAPPED": 0,
                "Q_KEY_CODE_SHIFT": 1,
                "Q_KEY_CODE_SHIFT_R": 2,
                "Q_KEY_CODE_ALT": 3,
                "Q_KEY_CODE_ALT_R": 4,
                "Q_KEY_CODE_CTRL": 5,
                "Q_KEY_CODE_CTRL_R": 6,
                "Q_KEY_CODE_MENU": 7,
                "Q_KEY_CODE_ESC": 8,
                "Q_KEY_CODE_1": 9,
                "Q_KEY_CODE_2": 10,
                "Q_KEY_CODE_3": 11,
                "Q_KEY_CODE_4": 12,
                "Q_KEY_CODE_5": 13,
                "Q_KEY_CODE_6": 14,
                "Q_KEY_CODE_7": 15,
                "Q_KEY_CODE_8": 16,
                "Q_KEY_CODE_9": 17,
                "Q_KEY_CODE_0": 18,
                "Q_KEY_CODE_MINUS": 19,
                "Q_KEY_CODE_EQUAL": 20,
                "Q_KEY_CODE_BACKSPACE": 21,
                "Q_KEY_CODE_TAB": 22,
                "Q_KEY_CODE_Q": 23,
                "Q_KEY_CODE_W": 24,
                "Q_KEY_CODE_E": 25,
                "Q_KEY_CODE_R": 26,
                "Q_KEY_CODE_T": 27,
                "Q_KEY_CODE_Y": 28,
                "Q_KEY_CODE_U": 29,
                "Q_KEY_CODE_I": 30,
                "Q_KEY_CODE_O": 31,
                "Q_KEY_CODE_P": 32,
                "Q_KEY_CODE_BRACKET_LEFT": 33,
                "Q_KEY_CODE_BRACKET_RIGHT": 34,
                "Q_KEY_CODE_RET": 35,
                "Q_KEY_CODE_A": 36,
                "Q_KEY_CODE_S": 37,
                "Q_KEY_CODE_D": 38,
                "Q_KEY_CODE_F": 39,
                "Q_KEY_CODE_G": 40,
                "Q_KEY_CODE_H": 41,
                "Q_KEY_CODE_J": 42,
                "Q_KEY_CODE_K": 43,
                "Q_KEY_CODE_L": 44,
                "Q_KEY_CODE_SEMICOLON": 45,
                "Q_KEY_CODE_APOSTROPHE": 46,
                "Q_KEY_CODE_GRAVE_ACCENT": 47,
                "Q_KEY_CODE_BACKSLASH": 48,
                "Q_KEY_CODE_Z": 49,
                "Q_KEY_CODE_X": 50,
                "Q_KEY_CODE_C": 51,
                "Q_KEY_CODE_V": 52,
                "Q_KEY_CODE_B": 53,
                "Q_KEY_CODE_N": 54,
                "Q_KEY_CODE_M": 55,
                "Q_KEY_CODE_COMMA": 56,
                "Q_KEY_CODE_DOT": 57,
                "Q_KEY_CODE_SLASH": 58,
                "Q_KEY_CODE_ASTERISK": 59,
                "Q_KEY_CODE_SPC": 60,
                "Q_KEY_CODE_CAPS_LOCK": 61,
                "Q_KEY_CODE_F1": 62,
                "Q_KEY_CODE_F2": 63,
                "Q_KEY_CODE_F3": 64,
                "Q_KEY_CODE_F4": 65,
                "Q_KEY_CODE_F5": 66,
                "Q_KEY_CODE_F6": 67,
                "Q_KEY_CODE_F7": 68,
                "Q_KEY_CODE_F8": 69,
                "Q_KEY_CODE_F9": 70,
                "Q_KEY_CODE_F10": 71,
                "Q_KEY_CODE_NUM_LOCK": 72,
                "Q_KEY_CODE_SCROLL_LOCK": 73,
                "Q_KEY_CODE_KP_DIVIDE": 74,
                "Q_KEY_CODE_KP_MULTIPLY": 75,
                "Q_KEY_CODE_KP_SUBTRACT": 76,
                "Q_KEY_CODE_KP_ADD": 77,
                "Q_KEY_CODE_KP_ENTER": 78,
                "Q_KEY_CODE_KP_DECIMAL": 79,
                "Q_KEY_CODE_SYSRQ": 80,
                "Q_KEY_CODE_KP_0": 81,
                "Q_KEY_CODE_KP_1": 82,
                "Q_KEY_CODE_KP_2": 83,
                "Q_KEY_CODE_KP_3": 84,
                "Q_KEY_CODE_KP_4": 85,
                "Q_KEY_CODE_KP_5": 86,
                "Q_KEY_CODE_KP_6": 87,
                "Q_KEY_CODE_KP_7": 88,
                "Q_KEY_CODE_KP_8": 89,
                "Q_KEY_CODE_KP_9": 90,
                "Q_KEY_CODE_LESS": 91,
                "Q_KEY_CODE_F11": 92,
                "Q_KEY_CODE_F12": 93,
                "Q_KEY_CODE_PRINT": 94,
                "Q_KEY_CODE_HOME": 95,
                "Q_KEY_CODE_PGUP": 96,
                "Q_KEY_CODE_PGDN": 97,
                "Q_KEY_CODE_END": 98,
                "Q_KEY_CODE_LEFT": 99,
                "Q_KEY_CODE_UP": 100,
                "Q_KEY_CODE_DOWN": 101,
                "Q_KEY_CODE_RIGHT": 102,
                "Q_KEY_CODE_INSERT": 103,
                "Q_KEY_CODE_DELETE": 104,
                "Q_KEY_CODE_STOP": 105,
                "Q_KEY_CODE_AGAIN": 106,
                "Q_KEY_CODE_PROPS": 107,
                "Q_KEY_CODE_UNDO": 108,
                "Q_KEY_CODE_FRONT": 109,
                "Q_KEY_CODE_COPY": 110,
                "Q_KEY_CODE_OPEN": 111,
                "Q_KEY_CODE_PASTE": 112,
                "Q_KEY_CODE_FIND": 113,
                "Q_KEY_CODE_CUT": 114,
                "Q_KEY_CODE_LF": 115,
                "Q_KEY_CODE_HELP": 116,
                "Q_KEY_CODE_META_L": 117,
                "Q_KEY_CODE_META_R": 118,
                "Q_KEY_CODE_COMPOSE": 119,
                "Q_KEY_CODE_PAUSE": 120,
                "Q_KEY_CODE_RO": 121,
                "Q_KEY_CODE_HIRAGANA": 122,
                "Q_KEY_CODE_HENKAN": 123,
                "Q_KEY_CODE_YEN": 124,
                "Q_KEY_CODE_MUHENKAN": 125,
                "Q_KEY_CODE_KATAKANAHIRAGANA": 126,
                "Q_KEY_CODE_KP_COMMA": 127,
                "Q_KEY_CODE_KP_EQUALS": 128,
                "Q_KEY_CODE_POWER": 129,
                "Q_KEY_CODE_SLEEP": 130,
                "Q_KEY_CODE_WAKE": 131,
                "Q_KEY_CODE_AUDIONEXT": 132,
                "Q_KEY_CODE_AUDIOPREV": 133,
                "Q_KEY_CODE_AUDIOSTOP": 134,
                "Q_KEY_CODE_AUDIOPLAY": 135,
                "Q_KEY_CODE_AUDIOMUTE": 136,
                "Q_KEY_CODE_VOLUMEUP": 137,
                "Q_KEY_CODE_VOLUMEDOWN": 138,
                "Q_KEY_CODE_MEDIASELECT": 139,
                "Q_KEY_CODE_MAIL": 140,
                "Q_KEY_CODE_CALCULATOR": 141,
                "Q_KEY_CODE_COMPUTER": 142,
                "Q_KEY_CODE_AC_HOME": 143,
                "Q_KEY_CODE_AC_BACK": 144,
                "Q_KEY_CODE_AC_FORWARD": 145,
                "Q_KEY_CODE_AC_REFRESH": 146,
                "Q_KEY_CODE_AC_BOOKMARKS": 147,
                "Q_KEY_CODE_LANG1": 148,
                "Q_KEY_CODE_LANG2": 149,
                "Q_KEY_CODE_F13": 150,
                "Q_KEY_CODE_F14": 151,
                "Q_KEY_CODE_F15": 152,
                "Q_KEY_CODE_F16": 153,
                "Q_KEY_CODE_F17": 154,
                "Q_KEY_CODE_F18": 155,
                "Q_KEY_CODE_F19": 156,
                "Q_KEY_CODE_F20": 157,
                "Q_KEY_CODE_F21": 158,
                "Q_KEY_CODE_F22": 159,
                "Q_KEY_CODE_F23": 160,
                "Q_KEY_CODE_F24": 161,
                "Q_KEY_CODE__MAX": 162
            }
        },
    }

    ButtonNameFromValue = {
        v: k
        for k, v in typData['InputButton']['constants'].items()
    }
    AxisNameFromValue = {
        v: k
        for k, v in typData['InputAxis']['constants'].items()
    }
    MultiTouchEventName = {
        v: k
        for k, v in typData['InputMultiTouchType']['constants'].items()
    }
    QKeyCodeToKey = {
        v: k[11:]
        for k, v in typData['QKeyCode']['constants'].items()
    }

    evt_type = read_dword(dumpfile)
    if evt_type == typData['InputEventKind']['constants']['INPUT_EVENT_KIND_KEY']:
        key_type = read_dword(dumpfile)
        if key_type == typData['KeyValueKind']['constants']['KEY_VALUE_KIND_NUMBER']:
            number = read_qword(dumpfile)
            down = read_byte(dumpfile)
            print_event(eid, name, f'INPUT_EVENT_KIND_KEY::KEY_VALUE_KIND_NUMBER number {number:#x} {"down" if down else "up"}')
        elif key_type == typData['KeyValueKind']['constants']['KEY_VALUE_KIND_QCODE']:
            qcode = read_dword(dumpfile)
            down = read_byte(dumpfile)
            print_event(eid, name, f'INPUT_EVENT_KIND_KEY::KEY_VALUE_KIND_QCODE qcode {qcode:#x} ({QKeyCodeToKey.get(qcode, "UnknownKey")}) {"down" if down else "up"}')
        else:
            raise Exception(f'Unknown Event Type {key_type} for INPUT_EVENT_KIND_KEY')
    elif evt_type == typData['InputEventKind']['constants']['INPUT_EVENT_KIND_BTN']:
        btn = read_dword(dumpfile)
        down = read_byte(dumpfile)
        print_event(eid, name, f'INPUT_EVENT_KIND_BTN button {btn:#x} ({ButtonNameFromValue.get(btn, "UnknownButton")}) {"down" if down else "up"}')
    elif evt_type == typData['InputEventKind']['constants']['INPUT_EVENT_KIND_REL']:
        axis = read_dword(dumpfile)
        value = read_qword(dumpfile)
        print_event(eid, name, f'INPUT_EVENT_KIND_REL axis {axis:#x} ({AxisNameFromValue.get(axis, "UnknownAxis")}) value {value:#x}')
    elif evt_type == typData['InputEventKind']['constants']['INPUT_EVENT_KIND_ABS']:
        axis = read_dword(dumpfile)
        value = read_qword(dumpfile)
        print_event(eid, name, f'INPUT_EVENT_KIND_ABS axis {axis:#x} ({AxisNameFromValue.get(axis, "UnknownAxis")}) value {value:#x}')
    elif evt_type == typData['InputEventKind']['constants']['INPUT_EVENT_KIND_MTT']:
        mtt_type = read_dword(dumpfile)
        slot = read_qword(dumpfile)
        tracking_id = read_qword(dumpfile)
        axis = read_dword(dumpfile)
        value = read_qword(dumpfile)
        print_event(eid, name, f'INPUT_EVENT_KIND_MTT type {mtt_type:#x} ({MultiTouchEventName.get(mtt_type, "UnknownType")}) slot {slot:#x} tracking_id {tracking_id:#x} axis {axis:#x} ({AxisNameFromValue.get(axis, "UnknownAxis")}) value {value:#x}')
    else:
        raise Exception(f'Unknown InputEventKind {evt_type}')
    return True

def decode_shutdown(eid, name, dumpfile):
    print_event(eid, name)
    return True

def decode_char_write(eid, name, dumpfile):
    res = read_dword(dumpfile)
    offset = read_dword(dumpfile)
    print_event(eid, name, "%d -> %d" % (offset, res))
    return True

def decode_audio_out(eid, name, dumpfile):
    audio_data = read_dword(dumpfile)
    print_event(eid, name, "%d" % (audio_data))
    return True

def decode_random(eid, name, dumpfile):
    ret = read_dword(dumpfile)
    size = read_dword(dumpfile)
    swallow_bytes(eid, name, dumpfile, size)
    if (ret):
        print_event(eid, name, "%d bytes (getrandom failed)" % (size))
    else:
        print_event(eid, name, "%d bytes" % (size))
    return True

def decode_clock(eid, name, dumpfile):
    clock_data = read_qword(dumpfile)
    print_event(eid, name, "0x%x" % (clock_data))
    return True

def __decode_checkpoint(eid, name, dumpfile, old):
    """Decode a checkpoint.

    Checkpoints contain a series of async events with their own specific data.
    """
    replay_state.set_checkpoint()
    # save event count as we peek ahead
    event_number = replay_state.event_count
    next_event = read_event(dumpfile)

    # if the next event is EVENT_ASYNC there are a bunch of
    # async events to read, otherwise we are done
    if (old and next_event == 3) or (not old and next_event >= 3 and next_event <= 9):
        print_event(eid, name, "more data follows", event_number)
    else:
        print_event(eid, name, "no additional data", event_number)

    replay_state.reuse_event(next_event)
    return True

def decode_checkpoint_old(eid, name, dumpfile):
    return __decode_checkpoint(eid, name, dumpfile, False)

def decode_checkpoint(eid, name, dumpfile):
    return __decode_checkpoint(eid, name, dumpfile, True)

def decode_checkpoint_init(eid, name, dumpfile):
    print_event(eid, name)
    return True

def decode_end(eid, name, dumpfile):
    print_event(eid, name)
    return False

# pre-MTTCG merge
v5_event_table = [Decoder(0, "EVENT_INSTRUCTION", decode_instruction),
                  Decoder(1, "EVENT_INTERRUPT", decode_interrupt),
                  Decoder(2, "EVENT_EXCEPTION", decode_plain),
                  Decoder(3, "EVENT_ASYNC", decode_async_old),
                  Decoder(4, "EVENT_SHUTDOWN", decode_unimp),
                  Decoder(5, "EVENT_CHAR_WRITE", decode_char_write),
                  Decoder(6, "EVENT_CHAR_READ_ALL", decode_unimp),
                  Decoder(7, "EVENT_CHAR_READ_ALL_ERROR", decode_unimp),
                  Decoder(8, "EVENT_CLOCK_HOST", decode_clock),
                  Decoder(9, "EVENT_CLOCK_VIRTUAL_RT", decode_clock),
                  Decoder(10, "EVENT_CP_CLOCK_WARP_START", decode_checkpoint),
                  Decoder(11, "EVENT_CP_CLOCK_WARP_ACCOUNT", decode_checkpoint),
                  Decoder(12, "EVENT_CP_RESET_REQUESTED", decode_checkpoint),
                  Decoder(13, "EVENT_CP_SUSPEND_REQUESTED", decode_checkpoint),
                  Decoder(14, "EVENT_CP_CLOCK_VIRTUAL", decode_checkpoint),
                  Decoder(15, "EVENT_CP_CLOCK_HOST", decode_checkpoint),
                  Decoder(16, "EVENT_CP_CLOCK_VIRTUAL_RT", decode_checkpoint),
                  Decoder(17, "EVENT_CP_INIT", decode_checkpoint_init),
                  Decoder(18, "EVENT_CP_RESET", decode_checkpoint),
]

# post-MTTCG merge, AUDIO support added
v6_event_table = [Decoder(0, "EVENT_INSTRUCTION", decode_instruction),
                  Decoder(1, "EVENT_INTERRUPT", decode_interrupt),
                  Decoder(2, "EVENT_EXCEPTION", decode_plain),
                  Decoder(3, "EVENT_ASYNC", decode_async_old),
                  Decoder(4, "EVENT_SHUTDOWN", decode_unimp),
                  Decoder(5, "EVENT_CHAR_WRITE", decode_char_write),
                  Decoder(6, "EVENT_CHAR_READ_ALL", decode_unimp),
                  Decoder(7, "EVENT_CHAR_READ_ALL_ERROR", decode_unimp),
                  Decoder(8, "EVENT_AUDIO_OUT", decode_audio_out),
                  Decoder(9, "EVENT_AUDIO_IN", decode_unimp),
                  Decoder(10, "EVENT_CLOCK_HOST", decode_clock),
                  Decoder(11, "EVENT_CLOCK_VIRTUAL_RT", decode_clock),
                  Decoder(12, "EVENT_CP_CLOCK_WARP_START", decode_checkpoint),
                  Decoder(13, "EVENT_CP_CLOCK_WARP_ACCOUNT", decode_checkpoint),
                  Decoder(14, "EVENT_CP_RESET_REQUESTED", decode_checkpoint),
                  Decoder(15, "EVENT_CP_SUSPEND_REQUESTED", decode_checkpoint),
                  Decoder(16, "EVENT_CP_CLOCK_VIRTUAL", decode_checkpoint),
                  Decoder(17, "EVENT_CP_CLOCK_HOST", decode_checkpoint),
                  Decoder(18, "EVENT_CP_CLOCK_VIRTUAL_RT", decode_checkpoint),
                  Decoder(19, "EVENT_CP_INIT", decode_checkpoint_init),
                  Decoder(20, "EVENT_CP_RESET", decode_checkpoint),
]

# Shutdown cause added
v7_event_table = [Decoder(0, "EVENT_INSTRUCTION", decode_instruction),
                  Decoder(1, "EVENT_INTERRUPT", decode_interrupt),
                  Decoder(2, "EVENT_EXCEPTION", decode_unimp),
                  Decoder(3, "EVENT_ASYNC", decode_async_old),
                  Decoder(4, "EVENT_SHUTDOWN", decode_unimp),
                  Decoder(5, "EVENT_SHUTDOWN_HOST_ERR", decode_unimp),
                  Decoder(6, "EVENT_SHUTDOWN_HOST_QMP", decode_unimp),
                  Decoder(7, "EVENT_SHUTDOWN_HOST_SIGNAL", decode_unimp),
                  Decoder(8, "EVENT_SHUTDOWN_HOST_UI", decode_unimp),
                  Decoder(9, "EVENT_SHUTDOWN_GUEST_SHUTDOWN", decode_unimp),
                  Decoder(10, "EVENT_SHUTDOWN_GUEST_RESET", decode_unimp),
                  Decoder(11, "EVENT_SHUTDOWN_GUEST_PANIC", decode_unimp),
                  Decoder(12, "EVENT_SHUTDOWN___MAX", decode_unimp),
                  Decoder(13, "EVENT_CHAR_WRITE", decode_char_write),
                  Decoder(14, "EVENT_CHAR_READ_ALL", decode_unimp),
                  Decoder(15, "EVENT_CHAR_READ_ALL_ERROR", decode_unimp),
                  Decoder(16, "EVENT_AUDIO_OUT", decode_audio_out),
                  Decoder(17, "EVENT_AUDIO_IN", decode_unimp),
                  Decoder(18, "EVENT_CLOCK_HOST", decode_clock),
                  Decoder(19, "EVENT_CLOCK_VIRTUAL_RT", decode_clock),
                  Decoder(20, "EVENT_CP_CLOCK_WARP_START", decode_checkpoint),
                  Decoder(21, "EVENT_CP_CLOCK_WARP_ACCOUNT", decode_checkpoint),
                  Decoder(22, "EVENT_CP_RESET_REQUESTED", decode_checkpoint),
                  Decoder(23, "EVENT_CP_SUSPEND_REQUESTED", decode_checkpoint),
                  Decoder(24, "EVENT_CP_CLOCK_VIRTUAL", decode_checkpoint),
                  Decoder(25, "EVENT_CP_CLOCK_HOST", decode_checkpoint),
                  Decoder(26, "EVENT_CP_CLOCK_VIRTUAL_RT", decode_checkpoint),
                  Decoder(27, "EVENT_CP_INIT", decode_checkpoint_init),
                  Decoder(28, "EVENT_CP_RESET", decode_checkpoint),
]

v12_event_table = [Decoder(0, "EVENT_INSTRUCTION", decode_instruction),
                  Decoder(1, "EVENT_INTERRUPT", decode_interrupt),
                  Decoder(2, "EVENT_EXCEPTION", decode_exception),
                  Decoder(3, "EVENT_ASYNC_BH", decode_async_bh),
                  Decoder(4, "EVENT_ASYNC_BH_ONESHOT", decode_async_bh_oneshot),
                  Decoder(5, "EVENT_ASYNC_INPUT", decode_async_input),
                  Decoder(6, "EVENT_ASYNC_INPUT_SYNC", decode_plain),
                  Decoder(7, "EVENT_ASYNC_CHAR_READ", decode_async_char_read),
                  Decoder(8, "EVENT_ASYNC_BLOCK", decode_async_block),
                  Decoder(9, "EVENT_ASYNC_NET", decode_async_net),
                  Decoder(10, "EVENT_SHUTDOWN", decode_shutdown),
                  Decoder(11, "EVENT_SHUTDOWN_HOST_ERR", decode_shutdown),
                  Decoder(12, "EVENT_SHUTDOWN_HOST_QMP_QUIT", decode_shutdown),
                  Decoder(13, "EVENT_SHUTDOWN_HOST_QMP_RESET", decode_shutdown),
                  Decoder(14, "EVENT_SHUTDOWN_HOST_SIGNAL", decode_shutdown),
                  Decoder(15, "EVENT_SHUTDOWN_HOST_UI", decode_shutdown),
                  Decoder(16, "EVENT_SHUTDOWN_GUEST_SHUTDOWN", decode_shutdown),
                  Decoder(17, "EVENT_SHUTDOWN_GUEST_RESET", decode_shutdown),
                  Decoder(18, "EVENT_SHUTDOWN_GUEST_PANIC", decode_shutdown),
                  Decoder(19, "EVENT_SHUTDOWN_SUBSYS_RESET", decode_shutdown),
                  Decoder(20, "EVENT_SHUTDOWN_SNAPSHOT_LOAD", decode_shutdown),
                  Decoder(21, "EVENT_SHUTDOWN___MAX", decode_shutdown),
                  Decoder(22, "EVENT_CHAR_WRITE", decode_char_write),
                  Decoder(23, "EVENT_CHAR_READ_ALL", decode_unimp),
                  Decoder(24, "EVENT_CHAR_READ_ALL_ERROR", decode_unimp),
                  Decoder(25, "EVENT_AUDIO_OUT", decode_audio_out),
                  Decoder(26, "EVENT_AUDIO_IN", decode_unimp),
                  Decoder(27, "EVENT_RANDOM", decode_random),
                  Decoder(28, "EVENT_CLOCK_HOST", decode_clock),
                  Decoder(29, "EVENT_CLOCK_VIRTUAL_RT", decode_clock),
                  Decoder(30, "EVENT_CP_CLOCK_WARP_START", decode_checkpoint),
                  Decoder(31, "EVENT_CP_CLOCK_WARP_ACCOUNT", decode_checkpoint),
                  Decoder(32, "EVENT_CP_RESET_REQUESTED", decode_checkpoint),
                  Decoder(33, "EVENT_CP_SUSPEND_REQUESTED", decode_checkpoint),
                  Decoder(34, "EVENT_CP_CLOCK_VIRTUAL", decode_checkpoint),
                  Decoder(35, "EVENT_CP_CLOCK_HOST", decode_checkpoint),
                  Decoder(36, "EVENT_CP_CLOCK_VIRTUAL_RT", decode_checkpoint),
                  Decoder(37, "EVENT_CP_INIT", decode_checkpoint_init),
                  Decoder(38, "EVENT_CP_RESET", decode_checkpoint),
                  Decoder(39, "EVENT_END", decode_end),
]

def parse_arguments():
    "Grab arguments for script"
    parser = argparse.ArgumentParser()
    parser.add_argument("-f", "--file", help='record/replay dump to read from',
                        required=True)
    return parser.parse_args()

def decode_file(filename):
    "Decode a record/replay dump"
    dumpfile = open(filename, "rb")
    dumpsize = path.getsize(filename)
    # read and throwaway the header
    version = read_dword(dumpfile)
    junk = read_qword(dumpfile)

    # see REPLAY_VERSION
    print("HEADER: version 0x%x" % (version))

    if version == 0xe0200c:
        event_decode_table = v12_event_table
        replay_state.checkpoint_start = 30
    elif version == 0xe02007:
        event_decode_table = v7_event_table
        replay_state.checkpoint_start = 12
    elif version == 0xe02006:
        event_decode_table = v6_event_table
        replay_state.checkpoint_start = 12
    else:
        event_decode_table = v5_event_table
        replay_state.checkpoint_start = 10

    try:
        decode_ok = True
        while decode_ok:
            event = read_event(dumpfile)
            decode_ok = call_decode(event_decode_table, event,
                                    dumpfile)
    except Exception as inst:
        print(f"error {inst}")
        sys.exit(1)

    finally:
        print(f"Reached {dumpfile.tell()} of {dumpsize} bytes")
        dumpfile.close()

if __name__ == "__main__":
    args = parse_arguments()
    decode_file(args.file)
