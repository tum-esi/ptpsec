import bitstruct
from os_ken.lib import stringify

from ptp.ptp_message_types import MessageType, SyncMsg, DelayReqMsg, FollowUpMsg, DelayRespMsg, AnnounceMsg, MeasurementMsg


class PtpPacket(stringify.StringifyMixin):
    _PACK_STR = ">"     \
                "u4"    \
                "u4"    \
                "u4"    \
                "u4"    \
                "u16"   \
                "u8"    \
                "u8"    \
                "r16"   \
                "s64"   \
                "r32"   \
                "u64"   \
                "u16"   \
                "u16"   \
                "u8"    \
                "s8"

    SUPPORTED_MSG_TYPES = {MessageType.SYNC: SyncMsg,
                           MessageType.DELAY_REQ: DelayReqMsg,
                           MessageType.FOLLOW_UP: FollowUpMsg,
                           MessageType.DELAY_RESP: DelayRespMsg,
                           MessageType.ANNOUNCE: AnnounceMsg,
                           MessageType.MEASUREMENT: MeasurementMsg}

    def __init__(self, data: bytes):
        (self.majorSdoId,
         messageType,
         self.minorVersionPTP,
         self.versionPTP,
         self.messageLength,
         self.domainNumber,
         self.minorSdoId,
         self.flagField,
         self.correctionField,
         self.messageTypeSpecific,
         self.sourceClockIdentity,
         self.sourcePortNumber,
         self.sequenceId,
         self.controlField,
         self.logMessageInterval) = bitstruct.unpack(PtpPacket._PACK_STR, data)

        self.messageType = MessageType(messageType)

        data = data[34:]

        self.msg = None

        if self.messageType in PtpPacket.SUPPORTED_MSG_TYPES:
            self.msg = PtpPacket.SUPPORTED_MSG_TYPES[self.messageType](data)
