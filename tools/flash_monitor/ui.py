"""Task-scoped Tk viewer. It never imports pyOCD or opens a probe."""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import time
import tkinter as tk
from tkinter import font as tkfont

from .model import EventTail, PHASES, TERMINAL, TaskState, atomic_json, process_alive


def blend(a: str, b: str, amount: float) -> str:
    amount = max(0, min(1, amount))
    left, right = [int(a[n:n + 2], 16) for n in (1, 3, 5)], [int(b[n:n + 2], 16) for n in (1, 3, 5)]
    return "#" + "".join(f"{round(x + (y - x) * amount):02x}" for x, y in zip(left, right))


def preferences() -> tuple[bool, bool]:
    """Follow Windows appearance and its reduced-animation preference."""
    dark, motion = False, True
    if os.name == "nt":
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                               r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize") as key:
                dark = winreg.QueryValueEx(key, "AppsUseLightTheme")[0] == 0
        except OSError:
            pass
        try:
            import ctypes
            animation = ctypes.c_int(1)
            if ctypes.windll.user32.SystemParametersInfoW(0x1042, 0, ctypes.byref(animation), 0):
                motion = motion and bool(animation.value)
        except (AttributeError, OSError):
            pass
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Control Panel\Desktop\WindowMetrics") as key:
                motion = motion and winreg.QueryValueEx(key, "MinAnimate")[0] != "0"
        except OSError:
            pass
    return dark, motion


def work_area(root) -> tuple[int, int, int, int]:
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        class MonitorInfo(ctypes.Structure):
            _fields_ = [("size", wintypes.DWORD), ("monitor", wintypes.RECT),
                        ("work", wintypes.RECT), ("flags", wintypes.DWORD)]
        user = ctypes.WinDLL("user32")
        user.MonitorFromPoint.argtypes = [wintypes.POINT, wintypes.DWORD]
        user.MonitorFromPoint.restype = wintypes.HANDLE
        user.GetMonitorInfoW.argtypes = [wintypes.HANDLE, ctypes.POINTER(MonitorInfo)]
        point = wintypes.POINT()
        user.GetCursorPos(ctypes.byref(point))
        monitor = user.MonitorFromPoint(point, 2)
        info = MonitorInfo()
        info.size = ctypes.sizeof(info)
        if user.GetMonitorInfoW(monitor, ctypes.byref(info)):
            return info.work.left, info.work.top, info.work.right, info.work.bottom
    return 0, 0, root.winfo_screenwidth(), root.winfo_screenheight()


class MonitorWindow:
    WIDTH, COMPACT, EXPANDED = 474, 142, 446

    def __init__(self, directory: Path):
        self.directory = directory
        self.task = json.loads((directory / "task.json").read_text(encoding="utf-8"))
        self.state = TaskState()
        self.tail = EventTail(directory / "events.jsonl")
        self.dark, self.motion = preferences()
        self.palette = ({"surface": "#242628", "subtle": "#2e3134", "ink": "#f0f2f3",
                         "muted": "#b5bec7", "line": "#454b51", "blue": "#79b9ee",
                         "green": "#78d5a7", "red": "#ff9e9e", "shadow": "#151718"}
                        if self.dark else
                        {"surface": "#ffffff", "subtle": "#f1f4f6", "ink": "#21272c",
                         "muted": "#606c76", "line": "#dce1e5", "blue": "#247bbb",
                         "green": "#19734f", "red": "#b03838", "shadow": "#c5cbd0"})
        self.root = tk.Tk()
        self.root.withdraw()
        self.root.title("Codex Flash Monitor" + (" · 模拟演示" if self.task["demo"] else ""))
        self.root.overrideredirect(True)
        self.root.attributes("-topmost", True)
        self.root.configure(bg="#fe00fd")
        if os.name == "nt":
            self.root.attributes("-transparentcolor", "#fe00fd")
        self.scale = max(1.0, self.root.winfo_fpixels("1i") / 96.0)
        self.width = round(self.WIDTH * self.scale)
        self.height = self.COMPACT
        self.height_from, self.height_target = self.height, self.height
        self.resize_started = 0.0
        self.expanded, self.pinned, self.closing = False, True, False
        self.last_frame = self.started = time.monotonic()
        self.last_poll, self.last_snapshot, self.last_owner_check = 0.0, 0.0, 0.0
        self.terminal_time, self.close_started = None, None
        self.progress_shown, self.phase_shown, self.phase_changed = 0.0, None, self.started
        self.last_event_time, self.drag = self.started, None
        self.last_logs = None
        self.mask = "#fe00fd"
        self.canvas = tk.Canvas(self.root, bg=self.mask, highlightthickness=0, bd=0)
        self.canvas.pack(fill="both", expand=True)
        self.fonts = {"normal": ("Microsoft YaHei UI", -round(13 * self.scale)),
                      "small": ("Microsoft YaHei UI", -round(11 * self.scale)),
                      "title": ("Microsoft YaHei UI", -round(15 * self.scale), "bold"),
                      "number": ("Segoe UI", -round(20 * self.scale)),
                      "mono": ("Consolas", -round(11 * self.scale))}
        self.measure = tkfont.Font(root=self.root, font=self.fonts["normal"])
        self.expand_button = self.button("展开详情  ▾", self.toggle_details)
        self.close_button = self.button("×", self.close, size=18)
        self.pin_button = self.button("已置顶", self.toggle_pin)
        self.log = tk.Text(self.root, bd=0, highlightthickness=0, relief="flat", wrap="word",
                           bg=self.palette["subtle"], fg=self.palette["muted"],
                           selectbackground=self.palette["blue"], font=self.fonts["mono"],
                           padx=10, pady=8, state="disabled", takefocus=True)
        self.canvas.bind("<ButtonPress-1>", self.begin_drag)
        self.canvas.bind("<B1-Motion>", self.move_drag)
        self.canvas.bind("<ButtonRelease-1>", lambda event: setattr(self, "drag", None))
        self.root.bind("<Escape>", lambda event: self.close())
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        left, top, right, bottom = work_area(self.root)
        self.x = max(left, right - self.width - round(22 * self.scale))
        self.bottom = bottom - round(22 * self.scale)
        self.area = (left, top, right, bottom)
        self._geometry(26 if self.motion else 0)
        self.root.attributes("-alpha", 0.0 if self.motion else 1.0)
        self.poll()
        self.draw(self.started)
        self.root.deiconify()
        self.write_snapshot()
        self.root.after(16, self.tick)

    def button(self, text, command, size=11):
        return tk.Button(self.root, text=text, command=command,
                         font=("Microsoft YaHei UI", -round(size * self.scale)),
                         bg=self.palette["surface"], fg=self.palette["muted"],
                         activebackground=self.palette["subtle"], activeforeground=self.palette["ink"],
                         relief="flat", bd=0, highlightthickness=0, takefocus=True, cursor="hand2")

    def p(self, value):
        return round(value * self.scale)

    def _geometry(self, slide=0):
        top = self.bottom - self.p(self.height) + self.p(slide)
        self.root.geometry(f"{self.width}x{self.p(self.height)}{self.x:+d}{top:+d}")

    def roundrect(self, x, y, width, height, radius, fill, outline="", line_width=1):
        x, y, width, height, radius = [self.p(v) for v in (x, y, width, height, radius)]
        self.canvas.create_polygon(x + radius, y, x + width - radius, y, x + width, y,
                                   x + width, y + radius, x + width, y + height - radius,
                                   x + width, y + height, x + width - radius, y + height,
                                   x + radius, y + height, x, y + height, x, y + height - radius,
                                   x, y + radius, x, y, smooth=True, splinesteps=24,
                                   fill=fill, outline=outline, width=self.p(line_width), tags="draw")

    def text(self, x, y, value, style="normal", fill=None, anchor="w", **kwargs):
        return self.canvas.create_text(self.p(x), self.p(y), text=value, font=self.fonts[style],
                                       fill=fill or self.palette["ink"], anchor=anchor, tags="draw", **kwargs)

    def fit(self, value: str, width: int) -> str:
        if self.measure.measure(value) <= self.p(width):
            return value
        while value and self.measure.measure(value + "…") > self.p(width):
            value = value[:-1]
        return value + "…"

    def toggle_details(self):
        self.expanded = not self.expanded
        self.height_from, self.resize_started = self.height, time.monotonic()
        available = (self.area[3] - self.area[1]) / self.scale - 44
        self.height_target = min(self.EXPANDED, available) if self.expanded else self.COMPACT
        self.expand_button.configure(text="收起详情  ▴" if self.expanded else "展开详情  ▾")
        self.write_snapshot()

    def toggle_pin(self):
        self.pinned = not self.pinned
        self.root.attributes("-topmost", self.pinned)
        self.pin_button.configure(text="已置顶" if self.pinned else "置顶")
        self.write_snapshot()

    def begin_drag(self, event):
        self.drag = (event.x_root - self.x, self.bottom - event.y_root)

    def move_drag(self, event):
        if self.drag:
            self.x = event.x_root - self.drag[0]
            self.bottom = event.y_root + self.drag[1]
            self._geometry()

    def write_snapshot(self, *, opened=True):
        try:
            atomic_json(self.directory / "ui.json", {"open": opened, "pid": os.getpid(),
                        "expanded": self.expanded, "pinned": self.pinned,
                        "width": self.WIDTH, "height": round(self.height), "scale": self.scale,
                        "phase": self.state.phase, "message": self.state.message,
                        "updated_at": time.time()})
        except OSError:
            pass

    def close(self):
        if not self.closing:
            self.closing, self.close_started = True, time.monotonic()

    def poll(self):
        for event in self.tail.read():
            if event.get("task") == self.task["id"] and self.state.apply(event):
                self.last_event_time = time.monotonic()
        for path in sorted((self.directory / "commands").glob("*.json")):
            try:
                command = json.loads(path.read_text(encoding="utf-8"))
                action = command.get("action")
                if action == "close":
                    self.close()
                elif action in {"expand", "collapse"} and (action == "expand") != self.expanded:
                    self.toggle_details()
                path.unlink(missing_ok=True)
            except (OSError, ValueError):
                continue

    def draw(self, now):
        c, s = self.palette, self.state
        self.canvas.delete("draw")
        self.roundrect(7, 9, self.WIDTH - 14, self.height - 14, 16, c["shadow"])
        self.roundrect(5, 4, self.WIDTH - 12, self.height - 14, 15, c["surface"], c["line"])
        terminal = s.phase in TERMINAL
        accent = c["green"] if s.phase == "done" else c["red"] if terminal else c["blue"]
        age = now - self.phase_changed
        pulse = .5 + .5 * math.sin(now * 3) if self.motion and not terminal else .1
        self.roundrect(23, 17, 16, 16, 5, blend(c["surface"], accent, .13 + pulse * .1))
        self.canvas.create_rectangle(self.p(28), self.p(22), self.p(34), self.p(28),
                                     outline=accent, width=self.p(1), tags="draw")
        self.text(47, 25, "Flash Monitor", "small", c["muted"])
        self.text(284, 25, "演示 · 无硬件" if self.task["demo"] else "CODEX TASK", "small", c["muted"])
        self.text(26, 57, PHASES[s.phase][0], "title", accent if terminal else c["ink"])
        value = ("✓" if s.phase == "done" else "!" if terminal else
                 f"{s.progress * 100:.0f}%" if s.progress is not None else "···")
        self.text(self.WIDTH - 29, 55, value, "number", accent, anchor="e")
        x, y, width = 26, 82, self.WIDTH - 54
        self.roundrect(x, y, width, 6, 3, c["subtle"])
        if s.progress is not None:
            fill_width = max(0, width * self.progress_shown)
            if fill_width >= 2:
                self.roundrect(x, y, fill_width, 6, 3, accent)
                if self.motion and not terminal:
                    # Glint stays inside the measured fill and never extends progress.
                    center = ((now * 140) % (width + 100)) - 50
                    for offset in range(-24, 25, 3):
                        spot = center + offset
                        if 3 < spot < fill_width - 3:
                            light = blend(accent, c["surface"], .38 * (1 - abs(offset) / 25))
                            self.canvas.create_line(self.p(x + spot), self.p(y + 1),
                                                    self.p(x + spot), self.p(y + 5),
                                                    fill=light, width=self.p(3), tags="draw")
        elif not terminal:
            start = (math.sin(now * 2) * .5 + .5) * (width - 60) if self.motion else 0
            self.roundrect(x + start, y, 60, 6, 3, blend(c["subtle"], accent, .7))
        elapsed = s.elapsed if terminal else s.elapsed + max(0, now - self.last_event_time)
        label = PHASES[s.phase][1]
        self.text(26, 111, f"{label} · {elapsed:.1f} s", "small", c["muted"])
        self.expand_button.place(x=self.p(self.WIDTH - 130), y=self.p(98), width=self.p(106), height=self.p(26))
        self.close_button.place(x=self.p(self.WIDTH - 44), y=self.p(11), width=self.p(24), height=self.p(25))
        self.pin_button.place_forget()
        if self.height > self.COMPACT + 12:
            self.canvas.create_line(self.p(24), self.p(140), self.p(self.WIDTH - 26), self.p(140),
                                    fill=c["line"], tags="draw")
            stages = ["connect", "erase", "program", "verify", "reset"]
            rank = stages.index(s.phase) if s.phase in stages else 5 if s.phase == "done" else -1
            for i, (phase, name) in enumerate(zip(stages, ["连接", "擦除", "写入", "校验", "启动"])):
                color = c["green"] if i < rank else accent if i == rank else c["muted"]
                mark = "✓" if i < rank else "●" if i == rank else "○"
                self.text(27 + i * 85, 162, f"{mark} {name}", "small", color)
            rows = [("目标", self.task["target"]), ("固件", Path(self.task["firmware"]).name),
                    ("连接", self.task["transport"])]
            for i, (label, text) in enumerate(rows):
                self.text(27, 192 + i * 24, label, "small", c["muted"])
                self.text(75, 192 + i * 24, self.fit(str(text), self.WIDTH - 112), "small")
            self.text(27, 266, self.fit(s.message, self.WIDTH - 60), "small", accent if terminal else c["muted"])
            log_height = min(107, self.height - 328)
            if log_height > 20:
                self.log.place(x=self.p(26), y=self.p(285), width=self.p(self.WIDTH - 54), height=self.p(log_height))
                content = "\n".join(s.logs)
                if content != self.last_logs:
                    self.log.configure(state="normal")
                    self.log.delete("1.0", "end")
                    self.log.insert("1.0", content)
                    self.log.configure(state="disabled")
                    self.log.see("end")
                    self.last_logs = content
                self.text(27, self.height - 32, "关闭窗口不取消烧录任务", "small", c["muted"])
                self.pin_button.place(x=self.p(self.WIDTH - 83), y=self.p(self.height - 45),
                                      width=self.p(58), height=self.p(25))
            else:
                self.log.place_forget()
        else:
            self.log.place_forget()
        if s.phase == "done" and self.motion and age < .9:
            # A single completion ripple, not a perpetual celebration.
            amount = min(1, age / .9)
            radius = 9 + 17 * amount
            self.canvas.create_oval(self.p(self.WIDTH - 40 - radius), self.p(55 - radius),
                                    self.p(self.WIDTH - 40 + radius), self.p(55 + radius),
                                    outline=blend(accent, c["surface"], amount), width=self.p(1), tags="draw")

    def tick(self):
        now = time.monotonic()
        dt, self.last_frame = min(.2, now - self.last_frame), now
        if now - self.last_poll >= .08:
            self.poll()
            self.last_poll = now
        if now - self.last_owner_check >= .7:
            if self.state.phase not in TERMINAL and not process_alive(self.task["owner_pid"]):
                self.state.phase, self.state.progress = "interrupted", None
                self.state.elapsed += now - self.last_event_time
                self.state.message = "烧录进程已退出，未收到完成结果"
                self.state.logs.append(self.state.message)
            self.last_owner_check = now
        if self.phase_shown != self.state.phase:
            self.phase_shown, self.phase_changed, self.progress_shown = self.state.phase, now, 0.0
        if self.state.phase in TERMINAL and self.terminal_time is None:
            self.terminal_time = now
        if self.terminal_time is not None:
            delay = self.task["success_seconds"] if self.state.phase == "done" else self.task["failure_seconds"]
            if now - self.terminal_time > delay:
                self.close()
        if now - self.started > 3600:
            self.close()  # A task viewer never becomes a permanent background app.
        target = self.state.progress or 0.0
        self.progress_shown = (min(target, self.progress_shown + (target - self.progress_shown) * (1 - math.exp(-dt * 14)))
                               if self.motion else target)
        if abs(self.height - self.height_target) > .1:
            amount = min(1, (now - self.resize_started) / .32) if self.motion else 1
            eased = 1 - (1 - amount) ** 3
            self.height = self.height_from + (self.height_target - self.height_from) * eased
        intro = min(1, (now - self.started) / .42) if self.motion else 1
        slide = 26 * (1 - intro) ** 3
        opacity = 1 - (1 - intro) ** 2
        if self.closing:
            outro = min(1, (now - self.close_started) / .18) if self.motion else 1
            opacity *= 1 - outro
            slide += 12 * outro ** 2
            if outro >= 1:
                self.write_snapshot(opened=False)
                self.root.destroy()
                return
        self._geometry(slide)
        self.root.attributes("-alpha", max(0, min(1, opacity)))
        self.draw(now)
        if now - self.last_snapshot > .6:
            self.write_snapshot()
            self.last_snapshot = now
        self.root.after(25, self.tick)


def run(directory: Path):
    if os.name == "nt":
        import ctypes
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except (AttributeError, OSError):
            pass
    window = MonitorWindow(directory)
    try:
        window.root.mainloop()
    finally:
        window.write_snapshot(opened=False)
