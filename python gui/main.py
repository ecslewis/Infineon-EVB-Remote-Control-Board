# main.py
import tkinter as tk
from tkinter import font as tkfont
from tkinter import ttk
import serial.tools.list_ports
from controller import RS485Controller


class App:
    def __init__(self, root):
        self.ctrl = RS485Controller()
        self.root = root
        self.root.title("BDS PWM Controller  ·  Infineon")
        self.root.geometry("420x960")
        self.root.minsize(360, 640)
        self.root.configure(bg="#FFFFFF")
        self.root.resizable(True, True)

        self.BG        = "#FFFFFF"
        self.SIDEBAR   = "#009B77"
        self.CARD      = "#F4F4F4"
        self.ACCENT    = "#009B77"
        self.DARK      = "#009B77"
        self.TEXT      = "#1A1A1A"
        self.SUBTEXT   = "#6B6B6B"
        self.GREEN     = "#009B77"
        self.RED       = "#E3000F"
        self.PURPLE    = "#009B77"
        self.BLUE      = "#0066CC"
        self.ORANGE    = "#009B77"
        self.LINE      = "#E0E0E0"

        self.f_title   = tkfont.Font(family="Helvetica", size=14, weight="bold")
        self.f_sub     = tkfont.Font(family="Helvetica", size=8)
        self.f_label   = tkfont.Font(family="Helvetica", size=8,  weight="bold")
        self.f_entry   = tkfont.Font(family="Helvetica", size=12)
        self.f_btn     = tkfont.Font(family="Helvetica", size=9,  weight="bold")
        self.f_tiny    = tkfont.Font(family="Helvetica", size=7)
        self.f_version = tkfont.Font(family="Helvetica", size=8)

        style = ttk.Style()
        style.theme_use("default")
        style.configure(
            "Port.TCombobox",
            fieldbackground = self.CARD,
            background      = self.CARD,
            foreground      = self.TEXT,
            arrowcolor      = self.ACCENT,
            padding         = 6
        )

        # ── outer wrapper ──────────────────────────────────────────────────
        outer = tk.Frame(root, bg=self.BG)
        outer.pack(fill="both", expand=True)

        # ── top bar ────────────────────────────────────────────────────────
        header = tk.Frame(outer, bg=self.SIDEBAR, height=56)
        header.pack(fill="x")
        header.pack_propagate(False)

        tk.Label(
            header, text="BDS PWM Controller",
            font=self.f_title, bg=self.SIDEBAR, fg="#FFFFFF"
        ).pack(side=tk.LEFT, padx=20, pady=15)

        tk.Label(
            header, text="dsPIC33EP32GS502",
            font=self.f_version, bg=self.SIDEBAR, fg="#CCECE6"
        ).pack(side=tk.RIGHT, padx=20)

        # ── scrollable canvas ──────────────────────────────────────────────
        canvas_frame = tk.Frame(outer, bg=self.BG)
        canvas_frame.pack(fill="both", expand=True)

        self.canvas = tk.Canvas(canvas_frame, bg=self.BG, highlightthickness=0)
        vscroll = ttk.Scrollbar(canvas_frame, orient="vertical",
                                command=self.canvas.yview)
        self.canvas.configure(yscrollcommand=vscroll.set)
        self.canvas.pack(side=tk.LEFT, fill="both", expand=True)
        vscroll.pack(side=tk.RIGHT, fill="y")

        inner = tk.Frame(self.canvas, bg=self.BG)
        self._cw = self.canvas.create_window((0, 0), window=inner, anchor="nw")

        inner.bind("<Configure>",
                   lambda e: self.canvas.configure(
                       scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>",
                         lambda e: self.canvas.itemconfig(
                             self._cw, width=e.width))
        self.canvas.bind_all("<MouseWheel>",
                             lambda e: self.canvas.yview_scroll(
                                 int(-1 * (e.delta / 120)), "units"))

        # ── main content frame ────────────────────────────────────────────
        c = tk.Frame(inner, bg=self.BG)
        c.pack(fill="both", expand=True, padx=24, pady=16)

        # ── interface section ─────────────────────────────────────────────
        conn_frame = tk.Frame(c, bg=self.BG)
        conn_frame.pack(fill="x", pady=(0, 16))

        tk.Label(
            conn_frame, text="INTERFACE",
            font=self.f_label, bg=self.BG, fg=self.SUBTEXT
        ).pack(anchor="w")

        conn_row = tk.Frame(conn_frame, bg=self.BG)
        conn_row.pack(fill="x", pady=(4, 0))

        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(
            conn_row,
            textvariable=self.port_var,
            font=self.f_version,
            state="readonly",
            style="Port.TCombobox"
        )
        self.port_combo.pack(side=tk.LEFT, expand=True, fill="x", padx=(0, 8))

        self.btn_refresh = tk.Button(
            conn_row, text="⟳",
            font=self.f_btn, bg=self.CARD, fg=self.TEXT,
            activebackground=self.LINE,
            bd=0, relief="flat", cursor="hand2", width=3,
            command=self.refresh_ports
        )
        self.btn_refresh.pack(side=tk.LEFT, padx=(0, 8))

        self.btn_connect = tk.Button(
            conn_row, text="Connect",
            font=self.f_btn, bg=self.ACCENT, fg="#FFFFFF",
            activebackground="#007A5E", activeforeground="#FFFFFF",
            bd=0, relief="flat", cursor="hand2",
            padx=16, pady=8,
            command=self.connect
        )
        self.btn_connect.pack(side=tk.RIGHT)

        self.port_info_label = tk.Label(
            conn_frame, text="RS485  ·  9600 baud",
            font=self.f_tiny, bg=self.BG, fg=self.SUBTEXT
        )
        self.port_info_label.pack(anchor="w", pady=(4, 0))

        self.refresh_ports()

        # ── divider ───────────────────────────────────────────────────────
        tk.Frame(c, bg=self.LINE, height=1).pack(fill="x", pady=8)

        # ── parameters ───────────────────────────────────────────────────
        tk.Label(
            c, text="PARAMETERS",
            font=self.f_label, bg=self.BG, fg=self.SUBTEXT
        ).pack(anchor="w", pady=(8, 8))

        param_card = tk.Frame(
            c, bg=self.CARD,
            highlightbackground=self.LINE, highlightthickness=1
        )
        param_card.pack(fill="x")

        self._param_row(param_card, "Frequency", "kHz", first=True)
        self.freq_entry = self._param_entry(param_card)
        tk.Frame(param_card, bg=self.LINE, height=1).pack(fill="x", padx=16)

        self._param_row(param_card, "Duty Cycle", "%")
        self.duty_entry = self._param_entry(param_card)
        self.duty_entry.insert(0, "50")
        tk.Frame(param_card, bg=self.LINE, height=1).pack(fill="x", padx=16)

        self._param_row(param_card, "Dead-Time", "ns")
        self.dt_entry = self._param_entry(param_card, last=True)
        self.dt_entry.insert(0, "100")

        # ── divider ───────────────────────────────────────────────────────
        tk.Frame(c, bg=self.LINE, height=1).pack(fill="x", pady=16)

        # ── operation mode ────────────────────────────────────────────────
        tk.Label(
            c, text="OPERATION MODE",
            font=self.f_label, bg=self.BG, fg=self.SUBTEXT
        ).pack(anchor="w", pady=(0, 8))

        mode_row = tk.Frame(c, bg=self.BG)
        mode_row.pack(fill="x", pady=(0, 8))

        self.btn_mode1 = tk.Button(
            mode_row, text="Mode 1\nAC_ZVS PWM",
            font=self.f_btn, bg=self.DARK, fg="#FFFFFF",
            activebackground="#FFFFFF", activeforeground="#FFFFFF",
            bd=0, relief="flat", cursor="hand2", state="disabled",
            command=self.start_mode1
        )
        self.btn_mode1.pack(side=tk.LEFT, expand=True, fill="x",
                            ipady=12, padx=(0, 6))

        self.btn_mode2 = tk.Button(
            mode_row, text="Mode 2\nDC-ZVS Control",
            font=self.f_btn, bg=self.PURPLE, fg="#FFFFFF",
            activebackground="#5A3A8A", activeforeground="#FFFFFF",
            bd=0, relief="flat", cursor="hand2", state="disabled",
            command=self.start_mode2
        )
        self.btn_mode2.pack(side=tk.RIGHT, expand=True, fill="x",
                            ipady=12, padx=(6, 0))

        # ── divider ───────────────────────────────────────────────────────
        tk.Frame(c, bg=self.LINE, height=1).pack(fill="x", pady=8)

        # ── actions ───────────────────────────────────────────────────────
        tk.Label(
            c, text="ACTIONS",
            font=self.f_label, bg=self.BG, fg=self.SUBTEXT
        ).pack(anchor="w", pady=(0, 8))

        action_row = tk.Frame(c, bg=self.BG)
        action_row.pack(fill="x", pady=(0, 8))

        self.btn_rdson = tk.Button(
            action_row, text="⚡  Measure Rdson",
            font=self.f_btn, bg=self.ORANGE, fg="#FFFFFF",
            activebackground="#C06400", activeforeground="#FFFFFF",
            bd=0, relief="flat", cursor="hand2", state="disabled",
            command=self.measure_rdson
        )
        self.btn_rdson.pack(side=tk.LEFT, expand=True, fill="x",
                            ipady=12, padx=(0, 6))

        self.btn_stop = tk.Button(
            action_row, text="■  Emergency Stop",
            font=self.f_btn, bg=self.RED, fg="#FFFFFF",
            activebackground="#B0000C", activeforeground="#FFFFFF",
            bd=0, relief="flat", cursor="hand2", state="disabled",
            command=self.pwm_stop
        )
        self.btn_stop.pack(side=tk.RIGHT, expand=True, fill="x",
                           ipady=12, padx=(6, 0))

        # ── divider ───────────────────────────────────────────────────────
        tk.Frame(c, bg=self.LINE, height=1).pack(fill="x", pady=8)

        # ── evb status ────────────────────────────────────────────────────
        tk.Label(
            c, text="EVB STATUS",
            font=self.f_label, bg=self.BG, fg=self.SUBTEXT
        ).pack(anchor="w", pady=(0, 8))

        evb_row = tk.Frame(c, bg=self.BG)
        evb_row.pack(fill="x", pady=(0, 8))

        self.btn_get_status = tk.Button(
            evb_row, text="⟳  Get EVB Status",
            font=self.f_btn, bg=self.BLUE, fg="#FFFFFF",
            activebackground="#0052A3", activeforeground="#FFFFFF",
            bd=0, relief="flat", cursor="hand2", state="disabled",
            command=self.get_evb_status
        )
        self.btn_get_status.pack(side=tk.LEFT, expand=True, fill="x",
                                 ipady=12, padx=(0, 6))

        self.btn_get_fw = tk.Button(
            evb_row, text="⟳  Get FW Version",
            font=self.f_btn, bg=self.BLUE, fg="#FFFFFF",
            activebackground="#0052A3", activeforeground="#FFFFFF",
            bd=0, relief="flat", cursor="hand2", state="disabled",
            command=self.get_fw_version
        )
        self.btn_get_fw.pack(side=tk.RIGHT, expand=True, fill="x",
                             ipady=12, padx=(6, 0))

        self.evb_status_label = tk.Label(
            c, text="─  No data",
            font=self.f_label, bg=self.CARD, fg=self.SUBTEXT,
            pady=10, padx=12, anchor="w"
        )
        self.evb_status_label.pack(fill="x", pady=(4, 2))

        self.fw_version_label = tk.Label(
            c, text="─  No data",
            font=self.f_label, bg=self.CARD, fg=self.SUBTEXT,
            pady=10, padx=12, anchor="w"
        )
        self.fw_version_label.pack(fill="x", pady=(2, 0))

        # ── status bar ────────────────────────────────────────────────────
        tk.Frame(c, bg=self.LINE, height=1).pack(fill="x", pady=8)

        status_row = tk.Frame(c, bg=self.BG)
        status_row.pack(fill="x")

        tk.Label(
            status_row, text="STATUS",
            font=self.f_label, bg=self.BG, fg=self.SUBTEXT
        ).pack(side=tk.LEFT)

        self.status = tk.Label(
            status_row, text="Not connected",
            font=self.f_version, bg=self.BG, fg=self.SUBTEXT
        )
        self.status.pack(side=tk.RIGHT)

        # ── footer ────────────────────────────────────────────────────────
        footer = tk.Frame(root, bg=self.CARD, height=32)
        footer.pack(fill="x", side=tk.BOTTOM)
        footer.pack_propagate(False)

        tk.Label(
            footer,
            text="Infineon Technologies  ·  BDS Control System  ·  v1.0",
            font=self.f_tiny, bg=self.CARD, fg=self.SUBTEXT
        ).pack(side=tk.LEFT, padx=16, pady=8)

        tk.Label(
            footer, text="© 2026",
            font=self.f_tiny, bg=self.CARD, fg=self.SUBTEXT
        ).pack(side=tk.RIGHT, padx=16)

    # ── ui helpers ────────────────────────────────────────────────────────
    def _param_row(self, parent, label, unit, first=False):
        row = tk.Frame(parent, bg=self.CARD)
        row.pack(fill="x", padx=16, pady=(12 if first else 8, 2))
        tk.Label(
            row, text=label.upper(),
            font=self.f_label, bg=self.CARD, fg=self.SUBTEXT
        ).pack(side=tk.LEFT)
        tk.Label(
            row, text=unit,
            font=self.f_tiny, bg=self.CARD, fg=self.ACCENT
        ).pack(side=tk.RIGHT)

    def _param_entry(self, parent, last=False):
        entry = tk.Entry(
            parent,
            font=self.f_entry, bd=0,
            bg=self.CARD, fg=self.TEXT,
            insertbackground=self.TEXT,
            relief="flat", state="disabled", justify="left"
        )
        entry.pack(fill="x", padx=16, ipady=6,
                   pady=(0, 12 if last else 4))
        return entry

    def _enable_controls(self):
        self.freq_entry.config(state="normal")
        self.duty_entry.config(state="normal")
        self.dt_entry.config(state="normal")
        self.btn_mode1.config(state="normal")
        self.btn_mode2.config(state="normal")
        self.btn_stop.config(state="normal")
        self.btn_rdson.config(state="normal")
        self.btn_get_status.config(state="normal")
        self.btn_get_fw.config(state="normal")

    def _disable_controls(self):
        self.freq_entry.config(state="disabled")
        self.duty_entry.config(state="disabled")
        self.dt_entry.config(state="disabled")
        self.btn_mode1.config(state="disabled")
        self.btn_mode2.config(state="disabled")
        self.btn_stop.config(state="disabled")
        self.btn_rdson.config(state="disabled")
        self.btn_get_status.config(state="disabled")
        self.btn_get_fw.config(state="disabled")

    # ── com port ──────────────────────────────────────────────────────────
    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports
        if ports:
            if self.port_var.get() not in ports:
                self.port_var.set(ports[0])
        else:
            self.port_var.set("")
            self.status.config(text="No COM ports found", fg=self.RED)

    # ── connect / disconnect ──────────────────────────────────────────────
    def connect(self):
        port = self.port_var.get()
        if not port:
            self.status.config(text="Error: No COM port selected",
                               fg=self.RED)
            return
        if self.ctrl.connect(port, baudrate=9600):
            self.status.config(
                text=f"Connected  ·  {port}  ·  9600 baud",
                fg=self.GREEN
            )
            self.port_info_label.config(
                text=f"{port}  ·  RS485  ·  9600 baud",
                fg=self.GREEN
            )
            self.port_combo.config(state="disabled")
            self.btn_refresh.config(state="disabled")
            self._enable_controls()
            self.btn_connect.config(
                text="Disconnect", bg=self.RED,
                command=self.disconnect
            )
        else:
            self.status.config(text="Connection failed", fg=self.RED)

    def disconnect(self):
        self.ctrl.disconnect()
        self._disable_controls()
        self.status.config(text="Disconnected", fg=self.SUBTEXT)
        self.port_info_label.config(
            text="RS485  ·  9600 baud", fg=self.SUBTEXT
        )
        self.port_combo.config(state="readonly")
        self.btn_refresh.config(state="normal")
        self.btn_connect.config(
            text="Connect", bg=self.ACCENT,
            command=self.connect
        )

    # ── helpers ───────────────────────────────────────────────────────────
    def _get_freq_duty(self):
        try:
            freq = int(self.freq_entry.get())
            duty = int(self.duty_entry.get())
            if not (1 <= freq <= 500):
                self.status.config(
                    text="Error: Frequency must be 1 – 500 kHz",
                    fg=self.RED)
                return None, None
            if not (0 <= duty <= 100):
                self.status.config(
                    text="Error: Duty cycle must be 0 – 100 %",
                    fg=self.RED)
                return None, None
            return freq, duty
        except ValueError:
            self.status.config(text="Error: Enter valid numbers",
                               fg=self.RED)
            return None, None

    # ── mode / action methods ─────────────────────────────────────────────
    def start_mode1(self):
        freq, duty = self._get_freq_duty()
        if freq is None:
            return
        try:
            dt = int(self.dt_entry.get())
            if not (0 <= dt <= 500):
                self.status.config(
                    text="Error: Dead-time must be 0 – 500 ns",
                    fg=self.RED)
                return
        except ValueError:
            self.status.config(text="Error: Enter valid dead-time",
                            fg=self.RED)
            return
        if self.ctrl.pwm_mode1(freq, duty, dt):
            self.status.config(
                text=f"Mode 1 active AC-ZVS  ·  {freq} kHz  ·  {duty}% duty  ·  {dt} ns DT",
                fg=self.GREEN)
        else:
            self.status.config(text="Error: Transmission failed",
                            fg=self.RED)
    def start_mode2(self):
        freq, duty = self._get_freq_duty()
        if freq is None:
            return
        try:
            dt = int(self.dt_entry.get())
            if not (0 <= dt <= 500):
                self.status.config(
                    text="Error: Dead-time must be 0 – 500 ns",
                    fg=self.RED)
                return
        except ValueError:
            self.status.config(text="Error: Enter valid dead-time",
                               fg=self.RED)
            return
        if self.ctrl.pwm_mode2(freq, duty, dt):
            self.status.config(
                text=f"Mode 2 active  ·  {freq} kHz  ·  {duty}%  ·  {dt} ns DT",
                fg=self.PURPLE)
        else:
            self.status.config(text="Error: Transmission failed",
                               fg=self.RED)

    def pwm_stop(self):
        if self.ctrl.pwm_stop():
            self.status.config(text="PWM stopped", fg=self.SUBTEXT)
        else:
            self.status.config(text="Error: Transmission failed",
                               fg=self.RED)

    def measure_rdson(self):
        if self.ctrl.measure_rdson():
            self.status.config(
                text="Rdson measurement triggered  ·  50 kHz single cycle",
                fg=self.ORANGE)
        else:
            self.status.config(text="Error: Transmission failed",
                               fg=self.RED)

    def get_evb_status(self):
        result = self.ctrl.get_status()
        if result is None:
            self.evb_status_label.config(
                text="✗  No response from EVB", fg=self.RED, bg=self.CARD)
            self.status.config(text="Error: No response", fg=self.RED)
        elif result == 0:
            self.evb_status_label.config(
                text="●  NORMAL", fg=self.GREEN, bg=self.CARD)
            self.status.config(text="EVB Status: Normal", fg=self.GREEN)
        elif result == 1:
            self.evb_status_label.config(
                text="⚠  ABNORMAL", fg=self.RED, bg=self.CARD)
            self.status.config(
                text="EVB Status: Abnormal — Check hardware", fg=self.RED)

    def get_fw_version(self):
        result = self.ctrl.get_firmware_version()
        if result is None:
            self.fw_version_label.config(
                text="✗  No response from EVB", fg=self.RED, bg=self.CARD)
            self.status.config(text="Error: No FW version response",
                               fg=self.RED)
        else:
            major, minor, patch = result
            self.fw_version_label.config(
                text=f"●  Firmware  v{major}.{minor}.{patch}",
                fg=self.GREEN, bg=self.CARD)
            self.status.config(
                text=f"Firmware: v{major}.{minor}.{patch}",
                fg=self.GREEN)


if __name__ == "__main__":
    root = tk.Tk()
    app  = App(root)
    root.mainloop()