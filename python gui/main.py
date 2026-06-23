# main.py
import tkinter as tk
from tkinter import font as tkfont
from controller import RS485Controller

class App:
    def __init__(self, root):
        self.ctrl = RS485Controller()
        self.root = root
        self.root.title("PWM Control")
        self.root.geometry("380x680")
        self.root.configure(bg="#F5EFE6")
        self.root.resizable(False, False)
        # ──────────────────────────────────────────
        # COLORS
        # ──────────────────────────────────────────
        self.BG      = "#F5EFE6"
        self.CARD    = "#FFFFFF"
        self.ACCENT1 = "#C9B8A8"
        self.ACCENT2 = "#A0856C"
        self.DARK    = "#5C4033"
        self.TEXT    = "#3E2723"
        self.SUBTEXT = "#A0856C"
        self.GREEN   = "#8DAF8A"
        self.RED     = "#C4786A"
        self.PURPLE  = "#9B8AB4"
        # ──────────────────────────────────────────
        # FONTS
        # ──────────────────────────────────────────
        self.f_title = tkfont.Font(family="Helvetica", size=16, weight="bold")
        self.f_sub   = tkfont.Font(family="Helvetica", size=9)
        self.f_label = tkfont.Font(family="Helvetica", size=9,  weight="bold")
        self.f_entry = tkfont.Font(family="Helvetica", size=13)
        self.f_btn   = tkfont.Font(family="Helvetica", size=10, weight="bold")
        # ──────────────────────────────────────────
        # BACKGROUND BLOBS
        # ──────────────────────────────────────────
        canvas = tk.Canvas(
            root,
            width              = 380,
            height             = 680,
            bg                 = self.BG,
            highlightthickness = 0
        )
        canvas.place(x=0, y=0)
        canvas.create_oval(-40, -40, 160, 160, fill="#E8DCCC", outline="")
        canvas.create_oval(260, 460, 430, 680, fill="#E8DCCC", outline="")
        canvas.create_oval(300, -20, 420, 100, fill="#D9C8B4", outline="")
        # ──────────────────────────────────────────
        # CONTAINER
        # ──────────────────────────────────────────
        c = tk.Frame(root, bg=self.BG)
        c.place(x=30, y=0, width=320, height=680)
        # ──────────────────────────────────────────
        # HEADER
        # ──────────────────────────────────────────
        tk.Label(
            c, text="PWM Control",
            font=self.f_title,
            bg=self.BG, fg=self.TEXT
        ).pack(pady=(35, 2))
        tk.Label(
            c, text="dsPIC33EP128GS808  ·  RS485",
            font=self.f_sub,
            bg=self.BG, fg=self.SUBTEXT
        ).pack(pady=(0, 15))
        # ──────────────────────────────────────────
        # INPUT CARD
        # ──────────────────────────────────────────
        card = tk.Frame(
            c, bg=self.CARD,
            highlightbackground="#E8DCCC",
            highlightthickness=1
        )
        card.pack(fill="x", pady=5)
        # Frequency
        fr = tk.Frame(card, bg=self.CARD)
        fr.pack(fill="x", padx=20, pady=(15, 2))
        tk.Label(fr, text="FREQUENCY", font=self.f_label,
                 bg=self.CARD, fg=self.SUBTEXT).pack(side=tk.LEFT)
        tk.Label(fr, text="kHz", font=self.f_sub,
                 bg=self.CARD, fg=self.ACCENT1).pack(side=tk.RIGHT)
        self.freq_entry = tk.Entry(
            card, font=self.f_entry, bd=0,
            bg="#FAF6F1", fg=self.TEXT,
            insertbackground=self.TEXT,
            relief="flat", state="disabled", justify="center"
        )
        self.freq_entry.pack(fill="x", padx=20, ipady=8, pady=(0,5))
        tk.Frame(card, bg="#F0EAE0", height=1).pack(fill="x", padx=20, pady=5)
        # Duty
        dr = tk.Frame(card, bg=self.CARD)
        dr.pack(fill="x", padx=20, pady=(8, 2))
        tk.Label(dr, text="DUTY CYCLE", font=self.f_label,
                 bg=self.CARD, fg=self.SUBTEXT).pack(side=tk.LEFT)
        tk.Label(dr, text="%", font=self.f_sub,
                 bg=self.CARD, fg=self.ACCENT1).pack(side=tk.RIGHT)
        self.duty_entry = tk.Entry(
            card, font=self.f_entry, bd=0,
            bg="#FAF6F1", fg=self.TEXT,
            insertbackground=self.TEXT,
            relief="flat", state="disabled", justify="center"
        )
        self.duty_entry.pack(fill="x", padx=20, ipady=8, pady=(0,5))
        self.duty_entry.insert(0, "50")
        tk.Frame(card, bg="#F0EAE0", height=1).pack(fill="x", padx=20, pady=5)
        # Dead-time
        dtr = tk.Frame(card, bg=self.CARD)
        dtr.pack(fill="x", padx=20, pady=(8, 2))
        tk.Label(dtr, text="DEAD-TIME", font=self.f_label,
                 bg=self.CARD, fg=self.SUBTEXT).pack(side=tk.LEFT)
        tk.Label(dtr, text="ns", font=self.f_sub,
                 bg=self.CARD, fg=self.ACCENT1).pack(side=tk.RIGHT)
        self.dt_entry = tk.Entry(
            card, font=self.f_entry, bd=0,
            bg="#FAF6F1", fg=self.TEXT,
            insertbackground=self.TEXT,
            relief="flat", state="disabled", justify="center"
        )
        self.dt_entry.pack(fill="x", padx=20, ipady=8, pady=(0,15))
        self.dt_entry.insert(0, "100")
        # ──────────────────────────────────────────
        # MODE BUTTONS
        # ──────────────────────────────────────────
        tk.Label(
            c, text="SELECT MODE",
            font=self.f_label,
            bg=self.BG, fg=self.SUBTEXT
        ).pack(anchor="w", pady=(10, 4))
        mode_row = tk.Frame(c, bg=self.BG)
        mode_row.pack(fill="x", pady=2)
        self.btn_mode1 = tk.Button(
            mode_row,
            text             = "Mode 1\nBoth Channels",
            font             = self.f_btn,
            bg               = self.DARK,
            fg               = "#FAF6F1",
            activebackground = self.ACCENT2,
            activeforeground = "#FAF6F1",
            bd               = 0,
            relief           = "flat",
            cursor           = "hand2",
            state            = "disabled",
            command          = self.start_mode1
        )
        self.btn_mode1.pack(
            side   = tk.LEFT,
            expand = True,
            fill   = "x",
            ipady  = 14,
            padx   = (0, 5)
        )
        self.btn_mode2 = tk.Button(
            mode_row,
            text             = "Mode 2\nZVS Control",
            font             = self.f_btn,
            bg               = self.PURPLE,
            fg               = "#FFFFFF",
            activebackground = "#8A79A3",
            activeforeground = "#FFFFFF",
            bd               = 0,
            relief           = "flat",
            cursor           = "hand2",
            state            = "disabled",
            command          = self.start_mode2
        )
        self.btn_mode2.pack(
            side   = tk.RIGHT,
            expand = True,
            fill   = "x",
            ipady  = 14,
            padx   = (5, 0)
        )
        # ──────────────────────────────────────────
        # STOP BUTTON
        # ──────────────────────────────────────────
        self.btn_stop = tk.Button(
            c,
            text             = "■  STOP",
            font             = self.f_btn,
            bg               = self.RED,
            fg               = "#FFFFFF",
            activebackground = "#B06B5E",
            activeforeground = "#FFFFFF",
            bd               = 0,
            relief           = "flat",
            cursor           = "hand2",
            state            = "disabled",
            command          = self.pwm_stop
        )
        self.btn_stop.pack(fill="x", pady=10, ipady=12)
        # ──────────────────────────────────────────
        # CONNECT BUTTON
        # ──────────────────────────────────────────
        self.btn_connect = tk.Button(
            c,
            text             = "Connect",
            font             = self.f_btn,
            bg               = self.ACCENT1,
            fg               = "#FFFFFF",
            activebackground = self.ACCENT2,
            activeforeground = "#FFFFFF",
            bd               = 0,
            relief           = "flat",
            cursor           = "hand2",
            command          = self.connect
        )
        self.btn_connect.pack(fill="x", pady=5, ipady=12)
        # ──────────────────────────────────────────
        # STATUS
        # ──────────────────────────────────────────
        self.status = tk.Label(
            c,
            text  = "⚪  Not connected",
            font  = self.f_sub,
            bg    = "#EDE4D8",
            fg    = self.SUBTEXT,
            padx  = 15,
            pady  = 6
        )
        self.status.pack(pady=8)
    # ──────────────────────────────────────────
    # HELPERS
    # ──────────────────────────────────────────
    def _get_freq_duty(self):
        try:
            freq = int(self.freq_entry.get())
            duty = int(self.duty_entry.get())
            if not (1 <= freq <= 500):
                self.status.config(
                    text = "🔴  Freq: 1 – 500 kHz",
                    fg   = self.RED
                )
                return None, None
            if not (0 <= duty <= 100):
                self.status.config(
                    text = "🔴  Duty: 0 – 100 %",
                    fg   = self.RED
                )
                return None, None
            return freq, duty
        except ValueError:
            self.status.config(
                text = "🔴  Enter valid numbers",
                fg   = self.RED
            )
            return None, None

    def _enable_controls(self):
        self.freq_entry.config(state  = "normal")
        self.duty_entry.config(state  = "normal")
        self.dt_entry.config(state    = "normal")
        self.btn_mode1.config(state   = "normal")
        self.btn_mode2.config(state   = "normal")
        self.btn_stop.config(state    = "normal")

    def _disable_controls(self):
        self.freq_entry.config(state  = "disabled")
        self.duty_entry.config(state  = "disabled")
        self.dt_entry.config(state    = "disabled")
        self.btn_mode1.config(state   = "disabled")
        self.btn_mode2.config(state   = "disabled")
        self.btn_stop.config(state    = "disabled")
    # ──────────────────────────────────────────
    # METHODS
    # ──────────────────────────────────────────
    def connect(self):
        if self.ctrl.connect("COM5", baudrate=9600):
            self.status.config(
                text = "🟢  Connected  ·  COM5",
                fg   = self.GREEN
            )
            self._enable_controls()
            self.btn_connect.config(
                text    = "Disconnect",
                bg      = self.RED,
                command = self.disconnect
            )
        else:
            self.status.config(
                text = "🔴  Connection failed",
                fg   = self.RED
            )

    def disconnect(self):
        self.ctrl.disconnect()
        self._disable_controls()
        self.status.config(
            text = "⚪  Not connected",
            fg   = self.SUBTEXT
        )
        self.btn_connect.config(
            text    = "Connect",
            bg      = self.ACCENT1,
            command = self.connect
        )

    def start_mode1(self):
        freq, duty = self._get_freq_duty()
        if freq is None:
            return
        if self.ctrl.pwm_mode1(freq, duty):
            self.status.config(
                text = f"🟢  Mode 1  ·  {freq} kHz  ·  {duty}%",
                fg   = self.GREEN
            )
        else:
            self.status.config(
                text = "🔴  Send failed",
                fg   = self.RED
            )

    def start_mode2(self):
        freq, duty = self._get_freq_duty()
        if freq is None:
            return
        try:
            dt = int(self.dt_entry.get())
            if not (0 <= dt <= 500):
                self.status.config(
                    text = "🔴  Dead-time: 0 – 500 ns",
                    fg   = self.RED
                )
                return
        except ValueError:
            self.status.config(
                text = "🔴  Enter valid dead-time",
                fg   = self.RED
            )
            return
        if self.ctrl.pwm_mode2(freq, duty, dt):
            self.status.config(
                text = f"🟣  Mode 2  ·  {freq} kHz  ·  {duty}%  ·  {dt}ns DT",
                fg   = self.PURPLE
            )
        else:
            self.status.config(
                text = "🔴  Send failed",
                fg   = self.RED
            )

    def pwm_stop(self):
        if self.ctrl.pwm_stop():
            self.status.config(
                text = "⚪  PWM Stopped",
                fg   = self.SUBTEXT
            )
        else:
            self.status.config(
                text = "🔴  Send failed",
                fg   = self.RED
            )

if __name__ == "__main__":
    root = tk.Tk()
    app  = App(root)
    root.mainloop()