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
    # ──────────────────────────────────────────
    def pwm_stop(self) -> bool:
        """Sends 0x02 to stop PWM"""
        return self._transmit(bytes([0x02]))

    # ──────────────────────────────────────────
    # CMD 0x03 - MODE 1
    # Both channels normal PWM
    # Packet: 0x03 FreqH FreqL Duty
    # ──────────────────────────────────────────
    def pwm_mode1(self, freq_khz: int, duty: int) -> bool:
        """
        Mode 1 - Both channels normal PWM
        freq_khz = 1 to 500
        duty     = 0 to 100
        """
        if not (1 <= freq_khz <= 500):
            return False
        if not (0 <= duty <= 100):
            return False

        freq_h = (freq_khz >> 8) & 0xFF
        freq_l =  freq_khz       & 0xFF

        return self._transmit(
            bytes([0x03, freq_h, freq_l, duty])
        )

    # ──────────────────────────────────────────
    # CMD 0x04 - MODE 2
    # PWM1 running + PWM2 constant high
    # Packet: 0x04 FreqH FreqL Duty
    # ──────────────────────────────────────────
    def pwm_mode2(self, freq_khz: int, duty: int, deadtime_ns: int = 0) -> bool:
        """
        Mode 2 - PWM1 normal + PWM2 constant high + deadtime
        freq_khz    = 1 to 500
        duty        = 0 to 100
        deadtime_ns = 0 to 500 (ns)
        """
        if not (1 <= freq_khz <= 500):
            return False
        if not (0 <= duty <= 100):
            return False
        if not (0 <= deadtime_ns <= 500):
            return False
        freq_h = (freq_khz    >> 8) & 0xFF
        freq_l =  freq_khz          & 0xFF
        dt_h   = (deadtime_ns >> 8) & 0xFF
        dt_l   =  deadtime_ns       & 0xFF
        return self._transmit(
            bytes([0x04, freq_h, freq_l, duty, dt_h, dt_l])
        )