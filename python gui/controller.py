# controller.py
import serial
import serial.tools.list_ports
import threading

class RS485Controller:
    def __init__(self):
        self.ser       = None
        self.connected = False
        self.lock      = threading.Lock()

    # ──────────────────────────────────────────
    # CRC TABLE - Matches MCU CRC table
    # ──────────────────────────────────────────
    _crc_table = [
        0x0000, 0xC1C0, 0x81C1, 0x4001, 0x01C3, 0xC003, 0x8002, 0x41C2,
        0x01C6, 0xC006, 0x8007, 0x41C7, 0x0005, 0xC1C5, 0x81C4, 0x4004,
        0x01CC, 0xC00C, 0x800D, 0x41CD, 0x000F, 0xC1CF, 0x81CE, 0x400E,
        0x000A, 0xC1CA, 0x81CB, 0x400B, 0x01C9, 0xC009, 0x8008, 0x41C8,
        0x01D8, 0xC018, 0x8019, 0x41D9, 0x001B, 0xC1DB, 0x81DA, 0x401A,
        0x001E, 0xC1DE, 0x81DF, 0x401F, 0x01DD, 0xC01D, 0x801C, 0x41DC,
        0x0014, 0xC1D4, 0x81D5, 0x4015, 0x01D7, 0xC017, 0x8016, 0x41D6,
        0x01D2, 0xC012, 0x8013, 0x41D3, 0x0011, 0xC1D1, 0x81D0, 0x4010,
        0x01F0, 0xC030, 0x8031, 0x41F1, 0x0033, 0xC1F3, 0x81F2, 0x4032,
        0x0036, 0xC1F6, 0x81F7, 0x4037, 0x01F5, 0xC035, 0x8034, 0x41F4,
        0x003C, 0xC1FC, 0x81FD, 0x403D, 0x01FF, 0xC03F, 0x803E, 0x41FE,
        0x01FA, 0xC03A, 0x803B, 0x41FB, 0x0039, 0xC1F9, 0x81F8, 0x4038,
        0x0028, 0xC1E8, 0x81E9, 0x4029, 0x01EB, 0xC02B, 0x802A, 0x41EA,
        0x01EE, 0xC02E, 0x802F, 0x41EF, 0x002D, 0xC1ED, 0x81EC, 0x402C,
        0x01E4, 0xC024, 0x8025, 0x41E5, 0x0027, 0xC1E7, 0x81E6, 0x4026,
        0x0022, 0xC1E2, 0x81E3, 0x4023, 0x01E1, 0xC021, 0x8020, 0x41E0,
        0x01A0, 0xC060, 0x8061, 0x41A1, 0x0063, 0xC1A3, 0x81A2, 0x4062,
        0x0066, 0xC1A6, 0x81A7, 0x4067, 0x01A5, 0xC065, 0x8064, 0x41A4,
        0x006C, 0xC1AC, 0x81AD, 0x406D, 0x01AF, 0xC06F, 0x806E, 0x41AE,
        0x01AA, 0xC06A, 0x806B, 0x41AB, 0x0069, 0xC1A9, 0x81A8, 0x4068,
        0x0078, 0xC1B8, 0x81B9, 0x4079, 0x01BB, 0xC07B, 0x807A, 0x41BA,
        0x01BE, 0xC07E, 0x807F, 0x41BF, 0x007D, 0xC1BD, 0x81BC, 0x407C,
        0x01B4, 0xC074, 0x8075, 0x41B5, 0x0077, 0xC1B7, 0x81B6, 0x4076,
        0x0072, 0xC1B2, 0x81B3, 0x4073, 0x01B1, 0xC071, 0x8070, 0x41B0,
        0x0050, 0xC190, 0x8191, 0x4051, 0x0193, 0xC053, 0x8052, 0x4192,
        0x0196, 0xC056, 0x8057, 0x4197, 0x0055, 0xC195, 0x8194, 0x4054,
        0x019C, 0xC05C, 0x805D, 0x419D, 0x005F, 0xC19F, 0x819E, 0x405E,
        0x005A, 0xC19A, 0x819B, 0x405B, 0x0199, 0xC059, 0x8058, 0x4198,
        0x0188, 0xC048, 0x8049, 0x4189, 0x004B, 0xC18B, 0x818A, 0x404A,
        0x004E, 0xC18E, 0x818F, 0x404F, 0x018D, 0xC04D, 0x804C, 0x418C,
        0x0044, 0xC184, 0x8185, 0x4045, 0x0187, 0xC047, 0x8046, 0x4186,
        0x0182, 0xC042, 0x8043, 0x4183, 0x0041, 0xC181, 0x8180, 0x4040
    ]

    # ──────────────────────────────────────────
    # CRC CALCULATION
    # ──────────────────────────────────────────
    def _crc16(self, data: bytes) -> int:
        """
        CRC16 matching MCU CrcValueByteCalc function
        """
        crc = 0xFFFF
        for byte in data:
            tmp = self._crc_table[(crc ^ byte) & 0xFF]
            crc = ((tmp & 0xFF) << 8) + ((tmp ^ crc) >> 8)
        return crc

    # ──────────────────────────────────────────
    # PACKET BUILDER
    # ──────────────────────────────────────────
    def _build_packet(self, cmd: int, data: list = []) -> bytes:
        """
        Builds full packet:
        [0xAB][0xAA][LEN][CMD][DATA...][CRCH][CRCL][0xCD]

        LEN = number of data bytes + 1 (for CMD)
        CRC = calculated over [0xAA][LEN][CMD][DATA]
        """
        length     = len(data) + 1           # CMD + DATA bytes
        payload    = [cmd] + data            # CMD followed by data

        # CRC over 0xAA + LEN + CMD + DATA
        crc_input  = bytes([0xAA, length] + payload)
        crc        = self._crc16(crc_input)

        crc_h      = (crc >> 8) & 0xFF
        crc_l      =  crc       & 0xFF

        packet     = (
            [0xAB, 0xAA, length] +
            payload              +
            [crc_h, crc_l]       +
            [0xCD]
        )
        return bytes(packet)

    # ──────────────────────────────────────────
    # CONNECTION
    # ──────────────────────────────────────────
    def connect(self, port: str, baudrate: int = 9600) -> bool:
        try:
            self.ser = serial.Serial(
                port     = port,
                baudrate = baudrate,
                bytesize = serial.EIGHTBITS,
                parity   = serial.PARITY_NONE,
                stopbits = serial.STOPBITS_ONE,
                timeout  = 1.0
            )
            self.connected = True
            return True
        except serial.SerialException:
            return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.connected = False

    def get_ports(self) -> list:
        return [p.device for p in serial.tools.list_ports.comports()]

    # ──────────────────────────────────────────
    # CORE TRANSMIT
    # ──────────────────────────────────────────
    def _transmit(self, data: bytes) -> bool:
        if not self.connected:
            return False
        with self.lock:
            try:
                self.ser.reset_input_buffer()
                self.ser.write(data)
                return True
            except serial.SerialException:
                return False

    # ──────────────────────────────────────────
    # CMD 0x02 - STOP PWM
    # Packet: [0xAB][0xAA][0x01][0x02][CRCH][CRCL][0xCD]
    # ──────────────────────────────────────────
    def pwm_stop(self) -> bool:
        packet = self._build_packet(0x14)
        print(f"DEBUG: PWM stopped")
        return self._transmit(packet)
    def get_status(self):
        if not self.connected:
            print("DEBUG: not connected")
            return None
        
        with self.lock:
            try:
                # Build and send
                packet = self._build_packet(0x03)
                print(f"DEBUG: sending packet: {packet.hex()}")
                self.ser.reset_input_buffer()
                self.ser.write(packet)
                
                # Give MCU time to process and respond
                import time
                time.sleep(0.1)
                
                # Read response
                response = self.ser.read(8)
                print(f"DEBUG: response length: {len(response)}")
                print(f"DEBUG: response bytes: {response.hex() if response else 'empty'}")
                
                if len(response) < 8:
                    print("DEBUG: response too short")
                    return None
                if response[0] != 0xAB or response[1] != 0xAA:
                    print(f"DEBUG: bad start bytes: {hex(response[0])} {hex(response[1])}")
                    return None
                if response[7] != 0xCD:
                    print(f"DEBUG: bad end byte: {hex(response[7])}")
                    return None
                if response[3] != 0x23:
                    print(f"DEBUG: wrong CMD back: {hex(response[3])}")
                    return None
                
                # Validate CRC
                crc_received = (response[5] << 8) | response[6]
                crc_calc     = self._crc16(bytes([
                    0xAA, response[2], response[3], response[4]
                ]))
                print(f"DEBUG: crc_received={hex(crc_received)} crc_calc={hex(crc_calc)}")
                if crc_calc != crc_received:
                    print("DEBUG: CRC mismatch")
                    return None
                
                print(f"DEBUG: status = {response[4]}")
                return response[4]
                
            except serial.SerialException as e:
                print(f"DEBUG: serial exception: {e}")
                return None

    # ──────────────────────────────────────────
    # CMD 0x03 - MODE 1
    # Packet: [0xAB][0xAA][0x04][0x03][FH][FL][Duty][CRCH][CRCL][0xCD]
    # ──────────────────────────────────────────
    def pwm_mode1(self, freq_khz: int, duty: int) -> bool:
        if not (1 <= freq_khz <= 500): return False
        if not (0 <= duty     <= 100): return False

        freq_h = (freq_khz >> 8) & 0xFF
        freq_l =  freq_khz       & 0xFF

        packet = self._build_packet(
            0x05,
            [freq_h, freq_l, duty]
        )
        print(f"DEBUG: PWM mode 1 set frequency:{freq_khz}kHz, duty:{duty}%")
        return self._transmit(packet)

    # ──────────────────────────────────────────
    # CMD 0x04 - MODE 2
    # Packet: [0xAB][0xAA][0x06][0x04][FH][FL][Duty][DtH][DtL][CRCH][CRCL][0xCD]
    # ──────────────────────────────────────────
    def pwm_mode2(self,
                  freq_khz   : int,
                  duty       : int,
                  deadtime_ns: int = 0) -> bool:
        if not (1 <= freq_khz    <= 500): return False
        if not (0 <= duty        <= 100): return False
        if not (0 <= deadtime_ns <= 500): return False

        freq_h = (freq_khz    >> 8) & 0xFF
        freq_l =  freq_khz          & 0xFF
        dt_h   = (deadtime_ns >> 8) & 0xFF
        dt_l   =  deadtime_ns       & 0xFF

        packet = self._build_packet(
            0x06,
            [freq_h, freq_l, duty, dt_h, dt_l]
        )
        print(f"DEBUG: PWM mode 1 set frequency:{freq_khz}kHz, duty:{duty}%, deadtime: {deadtime_ns}ns")
        return self._transmit(packet)
    def measure_rdson(self) -> bool:
        packet = self._build_packet(0x10)
        return self._transmit(packet)
    def get_firmware_version(self):
        if not self.connected:
            return None
        
        with self.lock:
            try:
                # Build and send request
                packet = self._build_packet(0x01)
                print(f"DEBUG: sending FW version request: {packet.hex()}")
                self.ser.reset_input_buffer()
                self.ser.write(packet)
                
                # Give MCU time to process and respond
                import time
                time.sleep(0.1)
                
                # Read response
                # [0xAB][0xAA][0x04][0x21][MAJOR][MINOR][PATCH][CRCH][CRCL][0xCD]
                response = self.ser.read(10)
                print(f"DEBUG: FW response length: {len(response)}")
                print(f"DEBUG: FW response bytes: {response.hex() if response else 'empty'}")
                
                if len(response) < 10:
                    print("DEBUG: FW response too short")
                    return None
                if response[0] != 0xAB or response[1] != 0xAA:
                    print(f"DEBUG: bad start bytes: {hex(response[0])} {hex(response[1])}")
                    return None
                if response[9] != 0xCD:
                    print(f"DEBUG: bad end byte: {hex(response[9])}")
                    return None
                if response[3] != 0x21:
                    print(f"DEBUG: wrong CMD back: {hex(response[3])}")
                    return None
                
                # Validate CRC over [AA][LEN][CMD][MAJOR][MINOR][PATCH]
                crc_received = (response[7] << 8) | response[8]
                crc_calc     = self._crc16(bytes([
                    0xAA, response[2], response[3],
                    response[4], response[5], response[6]
                ]))
                print(f"DEBUG: crc_received={hex(crc_received)} crc_calc={hex(crc_calc)}")
                if crc_calc != crc_received:
                    print("DEBUG: CRC mismatch")
                    return None
                
                major = response[4]
                minor = response[5]
                patch = response[6]
                print(f"DEBUG: FW version = {major}.{minor}.{patch}")
                return (major, minor, patch)
                
            except serial.SerialException as e:
                print(f"DEBUG: serial exception: {e}")
                return None
    # # continous polling
    # def ping(self) -> bool:
    #     """
    #     Try to ping EVB but skip if another command is in progress
    #     Returns True if alive, False if no response, None if busy
    #     """
    #     if not self.connected:
    #         return False
        
    #     # Try to acquire lock but don't wait
    #     acquired = self.lock.acquire(blocking=False)
    #     if not acquired:
    #         return True   # assume alive, someone else is using the line
        
    #     try:
    #         packet = self._build_packet(0x03)
    #         self.ser.reset_input_buffer()
    #         self.ser.write(packet)

    #         import time
    #         time.sleep(0.05)

    #         response = self.ser.read(8)
    #         return len(response) == 8 and response[0] == 0xAB

    #     except (serial.SerialException, OSError):
    #         self._handle_disconnect()
    #         return False

    #     finally:
    #         self.lock.release()   # always release