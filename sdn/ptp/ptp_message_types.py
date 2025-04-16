from enum import Enum
import bitstruct

from os_ken.lib import stringify


class MessageType(Enum):
    SYNC = 0x0
    DELAY_REQ = 0x1
    PDELAY_REQ = 0x2
    PDELAY_RESP = 0x3
    FOLLOW_UP = 0x8
    DELAY_RESP = 0x9
    PDELAY_RESP_FOLLOW_UP = 0xA
    ANNOUNCE = 0xB
    SIGNALING = 0xC
    MANAGEMENT = 0xD
    # PTPsec
    MEASUREMENT = 0x4


class AnnounceMsg(stringify.StringifyMixin):
    _PACK_STR = ">"     \
                "u80"   \
                "s16"   \
                "r8"    \
                "u8"    \
                "u32"   \
                "u8"    \
                "u64"   \
                "u16"   \
                "u8"

    def __init__(self, data: bytes):
        (self.originTimeStamp,
         self.currentUtcOffset,
         _,  # reserved
         self.grandmasterPriority1,
         self.grandmasterClockQuality,
         self.grandmasterPriority2,
         self.grandmasterIdentity,
         self.stepsRemoved,
         self.timeSource) = bitstruct.unpack(AnnounceMsg._PACK_STR, data)

class SyncMsg(stringify.StringifyMixin):
    _PACK_STR = ">u80"

    def __init__(self, data: bytes):
        self.originTimestamp = bitstruct.unpack(SyncMsg._PACK_STR, data)[0]

class DelayReqMsg(stringify.StringifyMixin):
    _PACK_STR = ">u80"

    def __init__(self, data: bytes):
        self.originTimestamp = bitstruct.unpack(DelayReqMsg._PACK_STR, data)[0]

class FollowUpMsg(stringify.StringifyMixin):
    _PACK_STR = ">u80"

    def __init__(self, data: bytes):
        self.originTimestamp = bitstruct.unpack(FollowUpMsg._PACK_STR, data)[0]

class DelayRespMsg(stringify.StringifyMixin):
    _PACK_STR = ">"     \
                "u80"   \
                "u64"   \
                "u16"

    def __init__(self, data: bytes):
        (self.receiveTimestamp,
         self.requestingClockIdentity,
         self.requestingPortNumber) = bitstruct.unpack(DelayRespMsg._PACK_STR, data)

class MeasurementType(Enum):
    MEAS_MEASUREMENT = 0
    MEAS_FOLLOW_UP = 1
    MEAS_TRANSPORT = 2

class MeasurementMsg(stringify.StringifyMixin):
    _PACK_STR = ">"     \
                "u80"   \
                "u64"   \
                "u16"

    def __init__(self, data: bytes):
        (self.timestamp,
         self.targetClockIdentity,
         self.measType) = bitstruct.unpack(MeasurementMsg._PACK_STR, data)

        self.measType = MeasurementType(self.measType)
