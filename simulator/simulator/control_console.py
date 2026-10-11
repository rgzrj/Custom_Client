"""Separate Chinese simulation control window; Python stdlib only."""

import copy
import argparse
import json
import os
import sys
import tempfile
import uuid
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from simulator.console_session import ConsoleSession, default_scene, scenario_steps, validate_scene
from simulator.sim_state import PHASES, ROLES, read_json

PHASE_LABELS = dict(zip(PHASES, ("准备", "自检", "倒计时", "比赛中", "已结束")))
BG, PANEL, FG, MUTED, ACCENT = "#09131c", "#12212d", "#e4edf3", "#93aabd", "#35c6d9"


class ControlConsole:
    def __init__(self, root):
        self.root = root
        self.session = ConsoleSession()
        self.closing = False
        self.vars = {}
        self.robot_vars = {}
        self.structure_vars = {}
        self.scrollers = []
        self.state = default_scene()["state"]
        root.title("RM27 本机联调控制台 · 项目模拟，非官方裁判系统")
        root.geometry("1120x820")
        root.minsize(1020, 740)
        root.configure(bg=BG)
        style = ttk.Style(root)
        style.theme_use("clam")
        style.configure(".", font=("Microsoft YaHei UI", 10), background=PANEL, foreground=FG)
        style.configure("TFrame", background=BG)
        style.configure("TLabel", background=BG, foreground=FG)
        style.configure("TLabelframe", background=BG, bordercolor="#2c4353")
        style.configure("TLabelframe.Label", background=BG, foreground=ACCENT)
        style.configure("TButton", padding=(14, 7), background="#213747", borderwidth=0)
        style.map("TButton", background=[("active", "#305367")])
        style.configure("Accent.TButton", background="#166273", foreground="#ffffff")
        style.map("Accent.TButton", background=[("active", "#23869a")])
        style.configure("TEntry", fieldbackground=PANEL, foreground=FG, insertcolor=FG)
        style.configure("TSpinbox", fieldbackground=PANEL, foreground=FG, arrowsize=15)
        style.configure("TCombobox", fieldbackground=PANEL, foreground=FG)
        style.map("TCombobox", fieldbackground=[("readonly", PANEL)],
                  foreground=[("readonly", FG)], selectbackground=[("readonly", PANEL)])
        style.configure("TCheckbutton", background=BG, foreground=FG)
        style.configure("TNotebook", background=BG, borderwidth=0)
        style.configure("TNotebook.Tab", padding=(18, 9), background=PANEL)
        style.map("TNotebook.Tab", background=[("selected", "#1b4555")],
                  foreground=[("selected", "#ffffff")])

        outer = ttk.Frame(root, padding=18)
        outer.pack(fill="both", expand=True)
        header = ttk.Frame(outer)
        header.pack(fill="x")
        ttk.Label(header, text="RM27 / 联调控制台", font=("Microsoft YaHei UI", 19, "bold")).pack(side="left")
        ttk.Label(header, text="独立开发工具 · 仅本机 · 不改变操作手 HUD", foreground=MUTED).pack(side="right")
        bar = ttk.Frame(outer, padding=(0, 12))
        bar.pack(fill="x")
        self.start_button = ttk.Button(bar, text="启动联调", style="Accent.TButton", command=self.start)
        self.start_button.pack(side="left")
        ttk.Button(bar, text="停止", command=self.session.stop).pack(side="left", padx=8)
        self.apply_button = ttk.Button(bar, text="应用编辑值", command=self.apply)
        self.apply_button.pack(side="left")
        self.status = tk.StringVar(value="未启动 · 修改值后点启动，或在运行时点应用")
        ttk.Label(bar, textvariable=self.status, foreground=ACCENT).pack(side="right")
        self.summary = tk.StringVar(value="UDP 39001 / MQTT 39004 · 探针 39002 / MPEG-TS 39003")
        ttk.Label(outer, textvariable=self.summary, foreground=MUTED,
                  wraplength=960).pack(anchor="w", pady=(0, 10))
        tabs = ttk.Notebook(outer)
        tabs.pack(fill="both", expand=True)
        self.match_tab = self.tab(tabs, "比赛与消息")
        self.robots_tab = self.tab(tabs, "双方机器人")
        self.video_tab = self.tab(tabs, "视频与故障")
        self.scene_tab = self.tab(tabs, "场景与回放")
        self.build_match()
        self.build_robots()
        self.build_video()
        self.build_scenes()
        ttk.Label(outer, text="运行记录 / 发送端时间线", foreground=MUTED).pack(anchor="w", pady=(10, 4))
        self.logs = tk.Text(outer, height=6, bg=PANEL, fg=FG, relief="flat",
                            font=("Microsoft YaHei UI", 9), state="disabled")
        self.logs.pack(fill="x")
        self.set_scene(default_scene())
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.bind("<MouseWheel>", self.scroll_tab)
        self.poll_id = root.after(100, self.poll)

    def tab(self, tabs, text):
        container = ttk.Frame(tabs)
        tabs.add(container, text=text)
        canvas = tk.Canvas(container, bg=BG, highlightthickness=0)
        scrollbar = ttk.Scrollbar(container, orient="vertical", command=canvas.yview)
        scrollbar.pack(side="right", fill="y")
        canvas.pack(side="left", fill="both", expand=True)
        canvas.configure(yscrollcommand=scrollbar.set)
        body = ttk.Frame(canvas, padding=16)
        window = canvas.create_window((0, 0), window=body, anchor="nw")
        body.bind("<Configure>", lambda _: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda event: canvas.itemconfigure(window, width=event.width))
        self.scrollers.append(canvas)
        return body

    def scroll_tab(self, event):
        if event.widget in (self.logs, self.steps):
            return
        for canvas in self.scrollers:
            if canvas.winfo_ismapped():
                canvas.yview_scroll(int(-event.delta / 120), "units")
                return "break"

    def field(self, parent, row, col, label, key, values=None, boolean=False):
        ttk.Label(parent, text=label).grid(row=row, column=col, sticky="w", padx=(0, 8), pady=8)
        var = tk.BooleanVar() if boolean else tk.StringVar()
        self.vars[key] = var
        if boolean:
            widget = ttk.Checkbutton(parent, variable=var)
        elif values:
            widget = ttk.Combobox(parent, textvariable=var, values=values, state="readonly", width=17)
        else:
            widget = ttk.Entry(parent, textvariable=var, width=19)
        widget.grid(row=row, column=col + 1, sticky="w", padx=(0, 24), pady=8)
        return widget

    def build_match(self):
        frame = self.match_tab
        self.field(frame, 0, 0, "我方阵营", "team", ("blue", "red"))
        self.field(frame, 0, 2, "本车", "self_index", ROLES)
        self.field(frame, 0, 4, "比赛阶段", "phase", tuple(PHASE_LABELS.values()))
        self.field(frame, 1, 0, "剩余秒数", "remaining_seconds")
        self.field(frame, 1, 2, "冻结模拟时钟", "paused", boolean=True)
        self.field(frame, 1, 4, "重启次数", "restart_count")
        self.field(frame, 2, 0, "红方金币", "red_coins")
        self.field(frame, 2, 2, "蓝方金币", "blue_coins")
        self.field(frame, 2, 4, "允许发弹量", "allowed_shots")
        for row, fields in enumerate((
                (("俯仰角 °", "pitch_deg"), ("偏航角 °", "yaw_deg"), ("弹速 m/s", "speed_mps")),
                (("底盘功率 W", "chassis_power_w"), ("缓冲能量 J", "buffer_energy_j"), ("枪管热量", "barrel_heat"))), 3):
            for col, (label, key) in enumerate(fields):
                self.field(frame, row, col * 2, label, key)
        for col, (label, key) in enumerate((("CLIMB 提示", "climb"), ("FIRE 提示", "fire"),
                                           ("SPEED UP 提示", "speed_up"))):
            self.field(frame, 5, col * 2, label, key, boolean=True)
        ttk.Label(frame, text="本车血量、护盾、等级、弹量在『双方机器人』编辑；血量为 0 即模拟阵亡。\n"
                  "时钟与阶段手动设定，不实现猜测的复活/兑换规则；当前 HUD 不显示剩余秒数。",
                  foreground=MUTED).grid(row=6, column=0, columnspan=6, sticky="w", pady=12)
        box = ttk.LabelFrame(frame, text="模拟消息 / 系统与机器人同时可测", padding=12)
        box.grid(row=7, column=0, columnspan=6, sticky="ew")
        self.category = tk.StringVar(value="system")
        ttk.Combobox(box, textvariable=self.category, values=("system", "robot"),
                     state="readonly", width=12).pack(side="left")
        self.event_text = tk.StringVar(value="模拟：飞镖命中目标")
        ttk.Entry(box, textvariable=self.event_text, width=52).pack(side="left", padx=10)
        self.event_button = ttk.Button(box, text="发送消息", command=self.send_event)
        self.event_button.pack(side="left")
        buildings = ttk.LabelFrame(frame, text="基地 / 前哨 · 项目模拟血量，可修改上限", padding=12)
        buildings.grid(row=8, column=0, columnspan=6, sticky="ew", pady=12)
        for row, (team, kind, label) in enumerate((
                ("red", "base", "红方基地"), ("blue", "base", "蓝方基地"),
                ("red", "outpost", "红方前哨"), ("blue", "outpost", "蓝方前哨"))):
            ttk.Label(buildings, text=label).grid(row=row, column=0, padx=8, pady=5)
            for col, key in enumerate(("hp", "max_hp"), 1):
                var = tk.StringVar()
                self.structure_vars[team, kind, key] = var
                ttk.Entry(buildings, textvariable=var, width=12).grid(row=row, column=col, padx=8)
        ttk.Label(buildings, text="每行：当前血量 / 最大血量；全部留空表示未知。\n"
                  "默认 5000/1500 仅用于演示，不是 RM2027 官方数值。", foreground=MUTED
                  ).grid(row=4, column=0, columnspan=3, sticky="w", pady=8)

    def build_robots(self):
        frame = self.robots_tab
        ttk.Label(frame, text="双方各六个 HUD 位置；阵营切换不会交换红蓝数据。本车与队伍栏共用同一行状态。",
                  foreground=MUTED).grid(row=0, column=0, columnspan=10, sticky="w", pady=(0, 12))
        fields = ("hp", "max_hp", "shield", "max_shield", "level", "ammo", "x", "y")
        for col, text in enumerate(("阵营", "机型", "血量", "最大血量", "护盾", "最大护盾", "等级", "弹量", "地图 X", "地图 Y")):
            ttk.Label(frame, text=text, foreground=ACCENT).grid(row=1, column=col, padx=6, pady=6)
        for t, team in enumerate(("red", "blue")):
            for index, role in enumerate(ROLES):
                row = 2 + t * 7 + index
                ttk.Label(frame, text="红方" if team == "red" else "蓝方",
                          foreground="#f27680" if team == "red" else ACCENT).grid(row=row, column=0)
                ttk.Label(frame, text=role).grid(row=row, column=1, padx=10)
                for col, key in enumerate(fields, 2):
                    var = tk.StringVar()
                    self.robot_vars[team, index, key] = var
                    ttk.Entry(frame, textvariable=var, width=8).grid(row=row, column=col, padx=5, pady=5)
        ttk.Label(frame, text="地图 X/Y 为 0–1 归一化坐标；不模拟真实车辆运动。血量不能超过最大血量，护盾同理。",
                  foreground=MUTED).grid(row=16, column=0, columnspan=10, sticky="w", pady=16)

    def build_video(self):
        frame = self.video_tab
        self.source_widgets = [
            self.field(frame, 0, 0, "裁判数据链路", "transport", ("udp", "mqtt")),
            self.field(frame, 0, 2, "视频模式", "video_mode", ("none", "probe", "ts"))]
        self.field(frame, 1, 0, "MPEG-TS 发送 kbps", "bitrate_kbps")
        self.field(frame, 1, 2, "随机种子", "seed")
        ttk.Label(frame, text="本地 .ts 视频").grid(row=2, column=0, sticky="w", pady=10)
        self.vars["video_path"] = tk.StringVar()
        ttk.Entry(frame, textvariable=self.vars["video_path"], width=62).grid(row=2, column=1, columnspan=3, sticky="w")
        self.source_widgets.append(ttk.Button(frame, text="选择文件", command=self.choose_video))
        self.source_widgets[-1].grid(row=2, column=4, padx=12)
        ttk.Label(frame, text="none：不发视频；probe：只测收包，不可解码；ts：实际 H.264/H.265 MPEG-TS 发包。\n"
                  "选择 ts 后，客户端用 -SimUdpVideo；选择 mqtt 后，客户端用 -SimMqtt。切源需停止后重启。",
                  foreground=MUTED).grid(row=3, column=0, columnspan=5, sticky="w", pady=12)
        for row, fields in enumerate(((("丢包 %", "loss_pct"), ("固定延迟 ms", "delay_ms")),
                                      (("抖动上限 ms", "jitter_ms"), ("乱序概率 %", "reorder_pct")),
                                      (("单比特损坏 %", "corrupt_pct"),)), 4):
            for col, (label, key) in enumerate(fields):
                self.field(frame, row, col * 2, label, key)
        ttk.Label(frame, text="故障参数只作用于视频包；队列最多 256 包。断流时丢弃旧队列，恢复 TS 从文件头重新同步。\n"
                  "乱序是概率性延后 60 ms，不保证每次都交换顺序；发送速率按给定码率估算，不是赛事延迟测量。",
                  foreground=MUTED).grid(row=7, column=0, columnspan=5, sticky="w", pady=12)
        buttons = ttk.Frame(frame)
        buttons.grid(row=8, column=0, columnspan=5, sticky="w")
        self.switch_buttons = []
        for key, text in (("referee_enabled", "裁判数据：停 / 恢复"),
                          ("video_enabled", "视频：断流 / 恢复"), ("mqtt_enabled", "MQTT：断线 / 重连")):
            button = ttk.Button(buttons, text=text, command=lambda k=key: self.switch(k))
            button.pack(side="left", padx=(0, 10))
            self.switch_buttons.append(button)

    def build_scenes(self):
        frame = self.scene_tab
        row = ttk.Frame(frame)
        row.pack(fill="x")
        self.preset = tk.StringVar(value="正常")
        ttk.Combobox(row, textvariable=self.preset, state="readonly", width=20,
                     values=("正常", "裁判过期", "独立断流恢复", "MQTT 断线重连")).pack(side="left")
        ttk.Button(row, text="填入预设步骤", command=self.fill_preset).pack(side="left", padx=8)
        ttk.Button(row, text="加载场景", command=self.load_scene).pack(side="left")
        ttk.Button(row, text="保存场景", command=self.save_scene).pack(side="left", padx=8)
        ttk.Label(frame, text="步骤使用相对时间 at_ms；只可切换链路、替换模拟状态或发送模拟事件。\n"
                  "场景在『启动联调』时执行一次；运行中『应用』不重新播放步骤。加载文件不会自动启动网络。",
                  foreground=MUTED).pack(anchor="w", pady=12)
        self.steps = tk.Text(frame, height=9, bg=PANEL, fg=FG, insertbackground=FG,
                             relief="flat", font=("Consolas", 10))
        self.steps.pack(fill="both", expand=True)
        record_row = ttk.Frame(frame)
        record_row.pack(fill="x", pady=12)
        self.record = tk.StringVar()
        ttk.Label(record_row, text="录制到").pack(side="left")
        ttk.Entry(record_row, textvariable=self.record, width=57).pack(side="left", padx=8)
        ttk.Button(record_row, text="选择新文件", command=self.choose_record).pack(side="left")
        ttk.Button(record_row, text="不录制", command=lambda: self.record.set("")).pack(side="left", padx=8)
        replay_row = ttk.Frame(frame)
        replay_row.pack(fill="x")
        ttk.Label(replay_row, text="回放倍速").pack(side="left")
        self.speed = tk.StringVar(value="1.0")
        ttk.Entry(replay_row, textvariable=self.speed, width=6).pack(side="left", padx=8)
        self.replay_button = ttk.Button(replay_row, text="选择记录并回放", command=self.replay)
        self.replay_button.pack(side="left")
        ttk.Label(frame, text="录制包含实际发送的裁判/视频包、控制操作和结束时间，保留故障后的字节与断流间隔。\n"
                  "64 MiB 上限；不覆盖旧记录。回放是发送端复现，不是客户端输入、规则门判定或真实比赛录像。",
                  foreground=MUTED).pack(anchor="w", pady=12)

    def set_scene(self, scene):
        scene = validate_scene(scene)
        state = self.state = scene["state"]
        for key, var in self.vars.items():
            if key == "self_index":
                value = ROLES[state[key]]
            elif key == "phase":
                value = PHASE_LABELS[state[key]]
            elif key in ("red_coins", "blue_coins"):
                value = state["coins"][key.split("_")[0]]
            elif key in state["actions"]:
                value = state["actions"][key]
            else:
                value = state.get(key, scene["settings"].get(key))
            var.set(value)
        for (team, index, key), var in self.robot_vars.items():
            robot = state["robots"][team][index]
            var.set(robot["position"][0 if key == "x" else 1] if key in ("x", "y") else robot[key])
        self.steps.delete("1.0", "end")
        for (team, kind, key), var in self.structure_vars.items():
            var.set(state.get("structures", {}).get(team, {}).get(kind, {}).get(key, ""))
        self.steps.insert("1.0", json.dumps(scene["steps"], ensure_ascii=False, indent=2))
        self.loaded_settings = scene["settings"]

    def collect(self):
        state = copy.deepcopy(self.state)
        settings = copy.deepcopy(self.loaded_settings)
        for key, var in self.vars.items():
            if key == "self_index":
                state[key] = ROLES.index(var.get())
            elif key == "phase":
                state[key] = next(k for k, label in PHASE_LABELS.items() if label == var.get())
            elif key in ("red_coins", "blue_coins"):
                state["coins"][key.split("_")[0]] = int(var.get())
            elif key in state["actions"]:
                state["actions"][key] = var.get()
            else:
                target = state if key in state else settings
                previous = target[key]
                target[key] = var.get() if type(previous) in (str, bool) else type(previous)(var.get())
        for (team, index, key), var in self.robot_vars.items():
            robot = state["robots"][team][index]
            if key in ("x", "y"):
                robot["position"][0 if key == "x" else 1] = float(var.get())
            else:
                robot[key] = int(var.get())
        if any(var.get().strip() for var in self.structure_vars.values()):
            state["structures"] = {
                team: {kind: {key: int(self.structure_vars[team, kind, key].get())
                              for key in ("hp", "max_hp")} for kind in ("base", "outpost")}
                for team in ("red", "blue")}
        else:
            state.pop("structures", None)
        scene = dict(format="rm27.sim.scene.v1", state=state, settings=settings,
                     steps=json.loads(self.steps.get("1.0", "end")))
        return validate_scene(scene)

    def attempt(self, action):
        try:
            action()
        except (ValueError, OSError, TypeError, tk.TclError) as error:
            messagebox.showerror("联调设置未应用", str(error), parent=self.root)

    def start(self):
        self.attempt(lambda: self.session.start(self.collect(), record=self.record.get() or None))

    def apply(self):
        self.attempt(lambda: self.session.post("scene", self.collect()))

    def send_event(self):
        self.attempt(lambda: self.session.post("event", dict(
            id="sim-" + uuid.uuid4().hex, category=self.category.get(), text=self.event_text.get())))

    def switch(self, key):
        def action():
            cfg = self.session.snapshot.get("settings", self.loaded_settings)
            if key == "mqtt_enabled" and cfg["transport"] != "mqtt":
                raise ValueError("当前不是 MQTT 链路；切换链路需先停止")
            value = not cfg[key]
            self.session.post("switch", {key: value})
            self.loaded_settings[key] = value
        self.attempt(action)

    def fill_preset(self):
        self.steps.delete("1.0", "end")
        self.steps.insert("1.0", json.dumps(scenario_steps(self.preset.get()), indent=2))

    def choose_video(self):
        path = filedialog.askopenfilename(parent=self.root, filetypes=[("MPEG-TS", "*.ts")])
        if path:
            self.vars["video_path"].set(path)
            self.vars["video_mode"].set("ts")

    def choose_record(self):
        path = filedialog.asksaveasfilename(parent=self.root, defaultextension=".rm27-replay",
                                          filetypes=[("模拟记录", "*.rm27-replay")])
        if path:
            if Path(path).exists():
                messagebox.showerror("请选择新文件", "录制不会覆盖旧文件，请换一个文件名。", parent=self.root)
            else:
                self.record.set(path)

    def replay(self):
        if self.session.running:
            messagebox.showinfo("先停止联调", "停止当前会话后再回放。", parent=self.root)
            return
        path = filedialog.askopenfilename(parent=self.root, filetypes=[("模拟记录", "*.rm27-replay")])
        if path:
            self.attempt(lambda: self.session.start(self.collect(), replay=path, speed=float(self.speed.get())))

    def load_scene(self):
        if self.session.running:
            messagebox.showinfo("先停止联调", "停止后再加载场景，避免覆盖正在编辑的状态。", parent=self.root)
            return
        path = filedialog.askopenfilename(parent=self.root, filetypes=[("模拟场景", "*.json")])
        if path:
            self.attempt(lambda: self.set_scene(read_json(Path(path))))

    def save_scene(self):
        def action():
            scene = self.collect()
            path = filedialog.asksaveasfilename(parent=self.root, defaultextension=".json",
                                              filetypes=[("模拟场景", "*.json")])
            if not path:
                return
            # Same-directory temporary file; a failed save preserves the old scene.
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=Path(path).parent,
                                                 delete=False) as stream:
                    temporary = Path(stream.name)
                    json.dump(scene, stream, ensure_ascii=False, allow_nan=False, indent=2)
                os.replace(temporary, path)
                self.session.log("场景已保存：" + path)
            finally:
                if temporary and temporary.exists():
                    temporary.unlink()
        self.attempt(action)

    def poll(self):
        while not self.session.messages.empty():
            text = self.session.messages.get_nowait()
            self.logs.configure(state="normal")
            self.logs.insert("end", text + "\n")
            if int(self.logs.index("end-1c").split(".")[0]) > 500:
                self.logs.delete("1.0", "2.0")
            self.logs.see("end")
            self.logs.configure(state="disabled")
        running = self.session.running
        snapshot = self.session.snapshot
        replay = running and snapshot.get("replay", False)
        self.start_button.configure(state="disabled" if running else "normal")
        self.replay_button.configure(state="disabled" if running else "normal")
        for widget in (self.apply_button, self.event_button, *self.switch_buttons):
            widget.configure(state="normal" if running and not replay else "disabled")
        for widget in self.source_widgets:
            widget.configure(state="disabled" if running else (
                "readonly" if isinstance(widget, ttk.Combobox) else "normal"))
        elapsed = snapshot.get("elapsed_ms", 0) / 1000
        self.status.set(f"{'回放' if replay else '运行' if running else '已停止'} · {elapsed:.1f} s")
        if "state" in snapshot and running:
            state, cfg = snapshot["state"], snapshot["settings"]
            self.loaded_settings.update({key: cfg[key] for key in (
                "referee_enabled", "video_enabled", "mqtt_enabled")})
            self.summary.set(f"我方 {'蓝' if state['team'] == 'blue' else '红'} · 本车 {ROLES[state['self_index']]} · "
                             f"剩余 {state['remaining_seconds']} s · {cfg['transport'].upper()} "
                             f"{'发送' if cfg['referee_enabled'] else '数据暂停'} · 视频 {cfg['video_mode']} "
                             f"{'发送' if cfg['video_enabled'] else '断流'} · MQTT "
                             f"{'允许连接' if cfg['mqtt_enabled'] else '断开'} · "
                             f"发包 {snapshot['sent']['referee']}/{snapshot['sent']['video']} · "
                             f"丢弃 {snapshot['dropped']} 损坏 {snapshot['corrupted']} 队列 {snapshot['queued']}")
        if self.closing and not running:
            self.root.destroy()
        else:
            self.poll_id = self.root.after(100, self.poll)

    def close(self):
        self.closing = True
        self.session.stop()
        self.root.withdraw()


def main():
    parser = argparse.ArgumentParser(description="RM27 本机模拟控制台")
    parser.add_argument("--self-test-report", type=Path, help="仅执行隐藏窗口离线自检，写入一个新 JSON 文件")
    args = parser.parse_args()
    root = tk.Tk()
    if args.self_test_report:
        root.withdraw()
    app = ControlConsole(root)
    if args.self_test_report:
        try:
            validate_scene(app.collect())
            root.update_idletasks()
            report = dict(ok=True, tkinter=root.tk.call("info", "patchlevel"),
                          networkStarted=False, cameraOpened=False,
                          frozen=bool(getattr(sys, "frozen", False)))
            with args.self_test_report.open("x", encoding="utf-8") as stream:
                json.dump(report, stream, ensure_ascii=False)
        finally:
            root.after_cancel(app.poll_id)
            root.destroy()
        return
    root.mainloop()


if __name__ == "__main__":
    main()
