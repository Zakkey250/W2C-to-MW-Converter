"""Windows desktop interface for the local W2C to MW Converter."""

import os
import sys
from pathlib import Path
from queue import Empty, Queue
from threading import Event, Thread
import tkinter as tk
from tkinter import filedialog, font, messagebox, ttk
import webbrowser

from build_world import CORE, STREAM, mw_inputs, mw_target_base
from convert import convert
from converter_locale import LANGUAGES, detail as localized_detail, locale_label, set_locale_label, tr
from install import (
    ASI, CONTROL, MANIFEST, SETTINGS, SUNSET_TRANSACTION, checked_package, install, install_sunset, installer_lock, recover,
    require_game_closed, unplaced_package_present,
)
from preflight import CATALOG, OPTIONAL_GAMEPLAY, carbon_row, check_target, checked_folder, runtime_rows, source_rows
from pursuit import validate_vault_tool
from setup_sourceworld import build_importer


SURFACE = "#0e0f08"
PANEL = "#1b1c14"
CARD = "#202018"
BORDER = "#47483b"
TEXT = "#f2f3e9"
MUTED = "#c8c7b7"
ACCENT = "#d1e895"
WARNING = "#f1c27a"
ERROR = "#ffb4a9"
STEPS = ("原本の選択", "導入先の検査", "変更内容の確認", "変換と配置")
QUALITY_DETAILS = ("標準の World テクスチャ", "高品質の World テクスチャ", "高品質の Carbon・MW テクスチャ")
FEATURE_LIMITATIONS = {
    "Dynamic physics and breakage for unmatched props": "MW原本と一致しない小物の移動・当たり判定・破壊",
    "Carbon pursuit breakers and their cameras": "Carbon固有のパースートブレーカーと専用カメラ",
}
ASSETS = Path(__file__).resolve().with_name("assets")
SUNSET_PRESET = Path(__file__).resolve().parent / "presets/SunSet"
WINDOWS = Path(os.environ.get("WINDIR", r"C:\Windows"))


class ConversionCancelled(Exception):
    pass


class ConverterWindow:
    def __init__(self, root):
        self.root = root
        self.language = tk.StringVar(master=root, value=locale_label())
        self.root.title("W2C to MW 2.0.0 — Converter RC6")
        self.root.geometry("1060x725")
        self.root.minsize(960, 665)
        self.root.configure(bg=SURFACE)
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.paths = {name: tk.StringVar() for name in ("w2c", "mw", "mw_reference", "carbon", "vault_tool", "output")}
        self.quality = tk.StringVar(value="NORMAL")
        self.auto_install = tk.BooleanVar()
        self.install_addon = tk.BooleanVar()
        self.install_lbx64 = tk.BooleanVar()
        self.install_vcache = tk.BooleanVar()
        self.cancellation = Event()
        self.events = Queue()
        self.busy = False
        self.can_cancel = False
        self.result = None
        self.existing_package = None
        self.font = "Noto Sans JP" if "Noto Sans JP" in font.families() else "Yu Gothic UI"
        self.heading_font = "Geist" if "Geist" in font.families() else "Segoe UI"
        self.radios = {state: tk.PhotoImage(file=ASSETS / f"radio-{state}.png")
                       for state in ("selected", "unselected")}
        self.configure_styles()
        self.show_inputs()
        self.root.after(80, self.poll)

    def configure_styles(self):
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure("TButton", font=(self.font, 10), padding=(15, 9), background=PANEL,
                        foreground=TEXT, bordercolor=BORDER, lightcolor=PANEL, darkcolor=PANEL)
        style.map("TButton", background=[("active", BORDER)], foreground=[("disabled", MUTED)])
        style.configure("Primary.TButton", background=ACCENT, foreground=SURFACE,
                        lightcolor=ACCENT, darkcolor=ACCENT)
        style.map("Primary.TButton", background=[("disabled", BORDER), ("active", "#e4f4b6")])
        style.configure("TCheckbutton", background=SURFACE, foreground=TEXT, font=(self.font, 10))
        style.map("TCheckbutton", background=[("active", SURFACE)], foreground=[("active", ACCENT)])
        style.configure("TEntry", padding=(12, 9), fieldbackground=SURFACE, foreground=TEXT,
                        insertcolor=TEXT, bordercolor=BORDER, lightcolor=BORDER, darkcolor=BORDER)
        style.configure("Horizontal.TProgressbar", troughcolor=BORDER, background=ACCENT,
                        bordercolor=SURFACE, lightcolor=ACCENT, darkcolor=ACCENT)

    def label(self, parent, text, *, fg=TEXT, bg=SURFACE, size=11, bold=False, family=None, **kwargs):
        return tk.Label(parent, text=text, fg=fg, bg=bg, anchor="w", justify="left",
                        font=(family or self.font, size, "bold" if bold else "normal"), **kwargs)

    def page(self, step, title, description, *, scroll=False):
        self.root.unbind("<MouseWheel>")
        for child in self.root.winfo_children():
            child.destroy()
        top = tk.Frame(self.root, bg=PANEL, height=68)
        top.pack(fill="x")
        top.pack_propagate(False)
        self.label(top, "W2C to MW Converter", bg=PANEL, size=17, bold=True, family=self.heading_font).pack(side="left", padx=(24, 15))
        self.label(top, "2.0.0", bg=PANEL, fg=ACCENT, size=9).pack(side="left")
        if step == 1:
            languages = ttk.Combobox(top, textvariable=self.language, values=LANGUAGES, state="readonly", width=11)
            languages.pack(side="right", padx=24)
            languages.bind("<<ComboboxSelected>>", self.change_language)
        else:
            self.label(top, tr('ローカル変換'), bg=PANEL, fg=MUTED, size=9).pack(side="right", padx=24)
        tk.Frame(self.root, bg=BORDER, height=1).pack(fill="x")
        self.footer = tk.Frame(self.root, bg=PANEL, height=76)
        self.footer.pack(side="bottom", fill="x")
        self.footer.pack_propagate(False)
        tk.Frame(self.root, bg=BORDER, height=1).pack(side="bottom", fill="x")
        rail = tk.Frame(self.root, width=224, bg=PANEL)
        rail.pack(side="left", fill="y")
        rail.pack_propagate(False)
        for number, name in enumerate(STEPS, 1):
            mark = "✓" if number < step else str(number)
            bg = CARD if number == step else PANEL
            row = tk.Frame(rail, bg=bg, height=46)
            row.pack(fill="x", padx=19, pady=(17 if number == 1 else 12, 0))
            row.pack_propagate(False)
            self.label(row, mark, bg=bg, fg=ACCENT if number <= step else MUTED, bold=True).pack(side="left", padx=10)
            self.label(row, tr(name), bg=bg, fg=TEXT if number <= step else MUTED, bold=True).pack(side="left")
        self.content = tk.Frame(self.root, bg=SURFACE)
        self.content.pack(fill="both", expand=True, padx=28, pady=24)
        self.label(self.content, title, size=24, bold=True).pack(fill="x")
        self.label(self.content, description, fg=MUTED, size=10, wraplength=680).pack(fill="x", pady=(10, 14))
        if scroll:
            area = tk.Frame(self.content, bg=SURFACE)
            area.pack(fill="both", expand=True)
            canvas = tk.Canvas(area, bg=SURFACE, highlightthickness=0, yscrollincrement=16)
            scrollbar = ttk.Scrollbar(area, orient="vertical", command=canvas.yview)
            scrollbar.pack(side="right", fill="y", padx=(8, 0))
            canvas.pack(side="left", fill="both", expand=True)
            canvas.configure(yscrollcommand=scrollbar.set)
            self.content = tk.Frame(canvas, bg=SURFACE)
            window = canvas.create_window(0, 0, anchor="nw", window=self.content)
            self.content.bind("<Configure>", lambda _: canvas.configure(scrollregion=canvas.bbox("all")))
            canvas.bind("<Configure>", lambda event: canvas.itemconfigure(window, width=event.width))
            self.root.bind("<MouseWheel>", lambda event: canvas.yview_scroll(-event.delta // 120, "units"))

    def button(self, title, command, *, primary=False, footer=False, state="normal"):
        button = ttk.Button(self.footer if footer else self.content, text=title, command=command,
                            style="Primary.TButton" if primary else "TButton", state=state)
        if footer:
            button.pack(side="right" if primary else "left", padx=20, pady=15)
        else:
            button.pack(anchor="w", pady=(0, 10))
        return button

    def field(self, title, key):
        self.label(self.content, title, size=10).pack(fill="x")
        row = tk.Frame(self.content, bg=SURFACE)
        row.pack(fill="x", pady=(4, 12))
        ttk.Entry(row, textvariable=self.paths[key]).pack(side="left", fill="x", expand=True)
        ttk.Button(row, text=tr('参照'), command=lambda: self.browse(key)).pack(side="left", padx=(12, 0))

    def browse(self, key):
        path = filedialog.askdirectory(parent=self.root, title=tr('フォルダーを選ぶ'))
        if path:
            self.paths[key].set(str(Path(path) / f"W2CMW-{self.quality.get()}") if key == "output" else path)

    def card(self, title, detail, status="", color=ACCENT):
        frame = tk.Frame(self.content, bg=PANEL, highlightthickness=1, highlightbackground=BORDER,
                         padx=12, pady=8)
        frame.pack(fill="x", pady=(0, 10))
        if status:
            self.label(frame, status, bg=PANEL, fg=color, bold=True).pack(side="right", padx=(10, 0))
        self.label(frame, tr(title), bg=PANEL, bold=True).pack(fill="x")
        self.label(frame, localized_detail(detail), bg=PANEL, fg=MUTED, size=9, wraplength=560).pack(fill="x")

    def show_inputs(self):
        self.existing_package = None
        self.page(1, tr('変換元を選ぶ'), tr('W2C 2.0.0の展開フォルダーと、使用する原本品質を指定してください。'), scroll=True)
        self.field(tr('W2C 2.0.0 原本（必須）'), "w2c")
        self.field(tr('Most Wanted の導入先（必須）'), "mw")
        self.field(tr('純正MWの参考元（導入先のワールドが改造済みの場合）'), "mw_reference")
        self.field(tr('Carbon のゲームフォルダー（任意）'), "carbon")
        self.field(tr('OGVI 1.4 のフォルダー（Carbonの装置・専用カメラを復元する場合）'), "vault_tool")
        self.label(self.content, tr('原本の品質'), bold=True).pack(fill="x", pady=(1, 8))
        quality_row = tk.Frame(self.content, bg=SURFACE)
        quality_row.pack(fill="x")
        choices = {}
        for column, (quality, detail) in enumerate(zip(CATALOG["qualities"], QUALITY_DETAILS)):
            quality_row.columnconfigure(column, weight=1, uniform="quality")
            option = tk.Frame(quality_row, bg=PANEL, highlightthickness=1, highlightbackground=BORDER, padx=10, pady=10)
            option.grid(row=0, column=column, sticky="ew", padx=(0 if column == 0 else 7, 0))
            radio = tk.Radiobutton(option, text=quality, variable=self.quality, value=quality, indicatoron=False,
                                   compound="left", font=(self.heading_font, 16, "bold"), bd=0, padx=0, pady=0,
                                   highlightbackground=BORDER, activeforeground=ACCENT, fg=TEXT)
            radio.pack(anchor="w")
            caption = self.label(option, tr(detail), fg=MUTED, bg=PANEL, size=9)
            caption.pack(anchor="w")
            choices[quality] = (option, radio, caption)

        def refresh_choices(*_):
            for quality, (option, radio, caption) in choices.items():
                selected = quality == self.quality.get()
                bg = CARD if selected else PANEL
                option.configure(bg=bg)
                caption.configure(bg=bg)
                radio.configure(image=self.radios["selected" if selected else "unselected"], bg=bg,
                                selectcolor=bg, activebackground=bg, fg=ACCENT if selected else TEXT)

        for _, radio, _ in choices.values():
            radio.configure(command=refresh_choices)
        for quality, (_, _, caption) in choices.items():
            caption.bind("<Button-1>", lambda _, value=quality: (self.quality.set(value), refresh_choices()))
        refresh_choices()
        self.button(tr('変換済みパックを配置'), self.choose_existing_package, footer=True)
        self.button(tr('導入先を検査'), self.check, primary=True, footer=True)

    def change_language(self, _event):
        set_locale_label(self.language.get())
        self.show_inputs()

    def get_folder(self, key, title, required=True):
        value = self.paths[key].get().strip()
        if not value and required:
            raise ValueError(tr('{0}を指定してください。', title))
        return checked_folder(value or None)

    def choose_existing_package(self):
        try:
            target = self.get_folder("mw", tr('Most Wanted の導入先'))
            loose = unplaced_package_present(target)
        except (OSError, ValueError) as error:
            messagebox.showerror(tr('導入先を確認'), localized_detail(str(error)), parent=self.root)
            return
        if loose and messagebox.askyesno(
            tr('コピー済みのパックを検出'),
            tr('導入先にまだ配置されていない変換済みパックがあります。\nこのパックを照合し、再変換せずに配置しますか？\n\n「いいえ」で別のパックを選べます。'), parent=self.root,
        ):
            self.use_copied_package(target)
            return
        path = filedialog.askdirectory(parent=self.root, title=tr('変換済みパックのフォルダーを選ぶ'))
        if path:
            self.existing_package = Path(path).resolve()
            self.check()

    def use_copied_package(self, target):
        self.existing_package = target
        self.check()

    def check(self):
        if self.busy:
            return
        try:
            mw = self.get_folder("mw", tr('Most Wanted の導入先'))
            self.mw_reference = self.get_folder("mw_reference", tr('純正MWの参考元'), required=False)
            w2c = None if self.existing_package else self.get_folder("w2c", tr('W2C 2.0.0原本'))
            carbon = self.get_folder("carbon", tr('Carbon 資源'), required=False) if not self.existing_package else None
            vault_tool = self.get_folder("vault_tool", "OGVI 1.4", required=False) if carbon else None
        except (OSError, ValueError) as error:
            messagebox.showerror(tr('入力を確認'), localized_detail(str(error)), parent=self.root)
            return
        quality, package = self.quality.get(), self.existing_package
        self.page(2, tr('導入先を検査'), tr('原本と対象の実行ファイル、必要なMODを照合しています。'))
        bar = ttk.Progressbar(self.content, mode="indeterminate")
        bar.pack(fill="x")
        bar.start(14)

        def task():
            target = check_target(mw)
            source = list(source_rows(w2c, quality)) if w2c else []
            manifest = checked_package(package, in_game=package == mw) if package else None
            native = None
            try:
                mw_target_base(mw)
                if manifest is None:
                    mw_inputs(mw, self.mw_reference)
                if vault_tool:
                    validate_vault_tool(vault_tool)
            except (OSError, ValueError) as error:
                native = str(error)
            return (w2c, mw, quality if manifest is None else manifest["quality"], source, target, native,
                    list(runtime_rows(mw, WINDOWS, target=target)), carbon, carbon_row(carbon), unplaced_package_present(mw), vault_tool)

        self.run(task, self.show_checks, self.show_inputs)

    def show_checks(self, result):
        (self.source, self.target, self.selected_quality, source, target, native, runtime,
         self.carbon_source, self.carbon, loose, self.vault_tool) = result
        blockers = [row for row in runtime if row["status"] in ("stop", "action")]
        self.warnings = [row for row in runtime if row["status"] == "warning"]
        bad_source = [row for row in source if row["status"] == "stop"]
        can_export = target["status"] == "ok" and not bad_source and native is None
        self.can_install = can_export and not blockers
        self.page(2, tr('導入先を検査'), tr('配置に必要な項目を確認してください。対応する導入先なら再検査できます。'), scroll=True)
        self.card("speed.exe", target["detail"], tr('適合') if target["status"] == "ok" else tr('要確認'),
                  ACCENT if target["status"] == "ok" else ERROR)
        if bad_source:
            self.card(f"W2C 2.0.0 · {self.selected_quality}", bad_source[0]["name"] + "\n" + bad_source[0]["detail"],
                      tr('{0}件 要確認', len(bad_source)), ERROR)
        else:
            self.card(f"{tr('変換済みパック') if self.existing_package else 'W2C 2.0.0'} · {self.selected_quality}",
                      tr('ファイルとSHA-256を照合しました。'), tr('確認済み'))
        if native:
            self.card(tr('MWの原本資源'), native, tr('要確認'), ERROR)
        if not self.existing_package:
            self.card("Carbon", self.carbon["detail"])
        if self.carbon_source and not self.existing_package:
            if self.vault_tool and native is None:
                self.card(tr('パースートブレーカーと専用カメラ'), tr('Carbon原本とOGVIを使って復元します。'), tr('有効'))
            elif not self.vault_tool:
                self.card(tr('パースートブレーカーと専用カメラ'),
                          tr('復元する場合はOGVI 1.4を指定してください。このまま進むとCarbonの画像資源だけを補います。'),
                          tr('OGVI未指定'), WARNING)
            self.button(tr('OGVI 1.4の配布元'), lambda: webbrowser.open("https://nfsmods.xyz/mod/5290"))
        other_blockers = [item for item in blockers if item["name"] != "SunSet"]
        if other_blockers:
            first = other_blockers[0]
            self.card(first["name"], first["detail"], tr('{0}件 要対応', len(other_blockers)), ERROR)
            for item in other_blockers:
                if item.get("url"):
                    self.button(tr('{0} の配布元', item['name']), lambda url=item["url"]: webbrowser.open(url))
        else:
            loader = next(item for item in runtime if item["name"] == "Win32 ASI Loader")
            self.card(tr('Win32 ASIローダー・D3DX'),
                      localized_detail(loader["detail"]) + tr('\nWin32 D3DXを検出しました。'), tr('確認済み'))
            for item in runtime:
                if item["name"] == "Redux 3.04 / NextGenGraphics":
                    self.card(item["name"], item["detail"], tr('確認済み'))
        sunset = next(item for item in runtime if item["name"] == "SunSet")
        label = {"ok": tr('導入済み'), "recommend": tr('未導入'), "action": tr('配置前に要対応')}[sunset["status"]]
        self.card(tr('SunSet（導入する場合はTexWizard必須）'), sunset["detail"], label,
                  {"ok": ACCENT, "recommend": WARNING, "action": ERROR}[sunset["status"]])
        links = tk.Frame(self.content, bg=SURFACE)
        links.pack(anchor="w", pady=(0, 10))
        ttk.Button(links, text=tr('SunSetの配布元'), command=lambda: webbrowser.open(sunset["url"])).pack(side="left", padx=(0, 10))
        ttk.Button(links, text=tr('TexWizard 1.3の配布元'), command=lambda: webbrowser.open(sunset["texWizardUrl"])).pack(side="left", padx=(0, 10))
        ttk.Button(links, text=tr('SunSetの推奨設定を開く'), command=lambda: os.startfile(SUNSET_PRESET)).pack(side="left")
        self.button(tr('SunSet・TexWizardを自動セットアップ'), self.setup_sunset)
        shadows = next(item for item in runtime if item["name"] == "ワールドの影")
        self.card(tr(shadows["name"]), shadows["detail"], tr('ゲーム内で確認'), WARNING)
        optional_names = {name for name, *_ in OPTIONAL_GAMEPLAY}
        optional = [item for item in runtime if item["name"] in optional_names]
        self.label(self.content, tr('任意MOD\u3000') + " / ".join(
            f"{item['name']}: {localized_detail(item['detail'])}" for item in optional),
            fg=MUTED, size=9, wraplength=660).pack(fill="x", pady=(0, 10))
        if loose and self.existing_package is None and not blockers and target["status"] == "ok" and native is None:
            self.button(tr('コピー済みパックを配置'), lambda: self.use_copied_package(self.target))
        if any(self.target.joinpath(name).is_file() for name in (f"{CONTROL}/transaction.json", SUNSET_TRANSACTION)):
            self.button(tr('中断した配置を復元'), self.recover_placement)
        self.button(tr('戻る'), self.show_inputs, footer=True)
        self.button(tr('再検査'), self.check, footer=True)
        self.button(tr('変更内容を確認'), self.show_plan, primary=True, footer=True,
                    state="normal" if can_export and (not self.existing_package or self.can_install) else "disabled")

    def setup_sunset(self):
        if self.busy:
            return
        self.page(2, tr('SunSetのセットアップ'),
                  tr('各配布元から入手したZIPを選んでください。すでに導入済みのMODは選択を省けます。'))
        catalog = CATALOG["recommendedSunSet"]
        selections = []
        for title, url in (("SunSet 1.15.2", catalog["url"]), (tr('TexWizard_MW.zip（1.3）'), catalog["texWizardUrl"])):
            value = tk.StringVar(master=self.root)
            selections.append(value)
            self.button(tr('{0} の配布元を開く', title), lambda link=url: webbrowser.open(link))
            line = tk.Frame(self.content, bg=SURFACE)
            line.pack(fill="x", pady=(0, 14))
            ttk.Entry(line, textvariable=value).pack(side="left", fill="x", expand=True)

            def browse_zip(selection=value, name=title):
                selected = filedialog.askopenfilename(parent=self.root, title=tr('{0} のZIPを選択', name),
                                                      filetypes=[("ZIP", "*.zip")])
                if selected:
                    selection.set(selected)

            ttk.Button(line, text=tr('ZIPを選択'), command=browse_zip).pack(side="left", padx=(10, 0))
        self.card(tr('配置先と変更内容'), tr('{0}\n不足している描画資源とMODを配置します。新規導入ではW2C用の設定を使います。既存の設定は保持し、必要な場合だけTexWizardにSunSetのパック参照を追加します。ゲームを終了してから進んでください。', self.target))
        reduce_glare = tk.BooleanVar(master=self.root, value=False)
        ttk.Checkbutton(self.content, text=tr('導入済みのSunSetでも白飛びを抑える（Bloomをオフ）'),
                        variable=reduce_glare).pack(anchor="w")

        def apply():
            paths = [Path(v.get().strip()).expanduser() if v.get().strip() else None for v in selections]
            glare = reduce_glare.get()
            self.page(2, tr('SunSetをセットアップ中'), tr('ZIPと導入先の資源を照合し、不足分を配置しています。'))
            self.run(lambda: install_sunset(self.target, *paths, reduce_existing_glare=glare),
                     lambda _: self.check(), self.setup_sunset)

        self.button(tr('戻る'), self.check, footer=True)
        self.button(tr('不足分を配置'), apply, primary=True, footer=True)

    def recover_placement(self):
        if self.busy:
            return
        if not messagebox.askyesno(tr('配置の復元'), tr('中断した配置を記録に従って復元しますか？'), parent=self.root):
            return

        def task():
            require_game_closed()
            with installer_lock(self.target):
                world = recover(self.target)
                return recover(self.target, sunset=True) or world

        self.page(2, tr('配置を復元中'), tr('変更記録と退避データを照合しています。'))
        self.run(task, lambda _: self.check(), self.check)

    def show_plan(self):
        if self.existing_package:
            description = tr('変換済みパックの配置先を確認してください。')
        elif self.vault_tool and self.carbon_source:
            description = tr('CarbonとW2C原本から、動く小物、パースートブレーカー、専用カメラを生成します。')
        else:
            description = tr('MW原本と一致する小物は接触で動きます。給油機はMWのモデルと爆発で代替し、Carbon固有の演出と専用カメラは省略します。')
        self.page(3, tr('変更内容を確認'), description, scroll=True)
        self.card(tr('ワールドマップとレーダー'), tr('W2C原本の輪郭地図を使用。'), tr('原本'))
        if self.existing_package:
            self.card(f"{tr('変換済みパック')} · {self.selected_quality}",
                      tr('ファイルとSHA-256を照合しました。'), tr('確認済み'))
        else:
            self.card(tr('Carbon 資源と装飾'), self.carbon["detail"],
                      tr('照合済み') if self.carbon.get("detected") else tr('W2C・MW原本'))
        if self.warnings:
            details = "\n".join(f"{item['name']}　{item['detail']}" + (" " + ", ".join(item["missing"]) if item.get("missing") else "")
                                for item in self.warnings)
            self.card(tr('検出したMODの設定'), details, tr('手動確認'), WARNING)
        else:
            self.card(tr('配置先と復元情報'), tr('{0}\n退避先は {2}/originals と {4}/rollback。', self.target, None, CONTROL, None, CONTROL), tr('配置時'))
        if not self.existing_package:
            if not self.paths["output"].get():
                self.paths["output"].set(str(Path.home() / f"W2CMW-{self.selected_quality}"))
            self.field(tr('出力先（新しく作るフォルダー）'), "output")
            self.auto_install.set(False)
            ttk.Checkbutton(self.content, variable=self.auto_install, text=tr('変換後に、選択したMWフォルダーへ自動配置'),
                            state="normal" if self.can_install else "disabled").pack(anchor="w")
            self.label(self.content, tr('オフの場合は保存のみ。ゲームへ反映するには配置が必要です。'),
                       fg=MUTED, size=9).pack(anchor="w", pady=(3, 0))
        ttk.Checkbutton(self.content, variable=self.install_addon,
                        text=tr('Addonのインストール — W2C 未開通区間の暫定開通')).pack(anchor="w", pady=(12, 4))
        ttk.Checkbutton(self.content, variable=self.install_lbx64,
                        text=tr('LBx64 RC3.1をインストール')).pack(anchor="w", pady=(0, 4))
        ttk.Checkbutton(self.content, variable=self.install_vcache,
                        text=tr('NFSMWVCacheAffinityをインストール')).pack(anchor="w", pady=(0, 4))
        self.label(self.content, tr('NFSMWVCacheAffinityには対応する英語版1.3と、L3容量が異なるCCDを持つAMD Ryzen X3Dが必要です。'),
                   fg=MUTED, size=9, wraplength=650).pack(anchor="w", pady=(0, 4))
        self.label(self.content, tr('未選択のコンポーネントも、導入ガイド付きでOptionalフォルダーに保存します。'),
                   fg=MUTED, size=9, wraplength=650).pack(anchor="w")
        self.label(self.content, tr('LBx64の導入時は旧LBを退避して差し替えます。DLSS・DLAAには対応するNVIDIA環境とSunSetが必要です。'),
                   fg=MUTED, size=9, wraplength=650).pack(anchor="w", pady=(4, 0))
        self.button(tr('戻る'), self.check, footer=True)
        self.button(tr('パックを配置') if self.existing_package else tr('ローカル変換を開始'),
                    self.begin, primary=True, footer=True)

    def confirm_placement(self):
        names = "\n".join((*CORE, tr('改造済みの場合は {0} も退避・置換', STREAM),
                            ASI, SETTINGS, MANIFEST, tr('{0}/generations/（ワールドパック）', CONTROL)))
        return messagebox.askyesno(tr('配置先を確認'), tr('{0}\n\n{2}\n\n変更前のファイルは {4} 内に退避されます。ゲームを閉じて続行してください。', self.target, None, names, None, CONTROL),
                                  parent=self.root)

    def place_exported(self):
        if self.can_install and self.confirm_placement():
            self.place()

    def begin(self):
        if (self.existing_package or self.auto_install.get()) and not self.confirm_placement():
            return
        if self.existing_package:
            self.output = self.existing_package
            self.result = None
            self.place()
            return
        value = self.paths["output"].get().strip()
        if not value:
            messagebox.showerror(tr('出力先を確認'), tr('新しい出力フォルダーを指定してください。'), parent=self.root)
            return
        self.output = Path(value).resolve()
        self.result = None
        importer = Path(__file__).with_name("bin") / "SourceWorld.exe"
        needs_setup = getattr(sys, "frozen", False) and not importer.is_file()
        if needs_setup:
            if self.language.get() == "日本語":
                title = "初回セットアップ"
                prompt = ("W2C原本の読み込みに必要な部品をこのPCでビルドします。"
                          "固定版のソースをGitHubから取得し、必要に応じてMicrosoftの.NET SDKも取得します。"
                          "ソースとSDKはハッシュで照合します。ゲーム資源やセーブは送信しません。\n\n続行しますか？")
            else:
                title = "First-time source importer setup"
                prompt = ("Build the W2C source importer on this PC. Setup downloads and verifies pinned "
                          "source from GitHub and, if needed, Microsoft's .NET SDK. It does not upload "
                          "game files or saves.\n\nContinue?")
            if not messagebox.askyesno(title, prompt, parent=self.root):
                return
        self.page(4, tr('ローカル変換中'), tr('検証が終わるまでお待ちください。'))
        self.card(tr('原本の照合'), f"W2C 2.0.0 · {self.selected_quality}", tr('完了'))
        self.stage_text = tk.StringVar(value=tr('変換を準備'))
        label = self.label(self.content, "", bold=True)
        label.configure(textvariable=self.stage_text)
        label.pack(fill="x", pady=10)
        self.bar = ttk.Progressbar(self.content, maximum=100)
        self.bar.pack(fill="x")
        self.cancel_button = self.button(tr('変換を中止'), self.cancel, footer=True)

        def progress(text, percent):
            if self.cancellation.is_set():
                raise ConversionCancelled()
            self.events.put(("progress", (text, percent)))

        def finished(result):
            self.result = result
            if self.auto_install.get() and not self.cancellation.is_set():
                self.place()
            else:
                self.complete()

        def task():
            if needs_setup:
                build_importer(importer, lambda label, value: progress(label, value // 10),
                               bundle=Path(__file__).with_name("importer-source"))
            return convert(self.source, self.target, self.selected_quality, self.output,
                           carbon=self.carbon_source, vault_tool=self.vault_tool,
                           mw_reference=self.mw_reference, progress=progress)

        self.run(task, finished, self.show_plan, cancellable=True)

    def place(self):
        self.page(4, tr('検証済みパックを配置中'), tr('配置先の状態を再検査し、変更前のファイルを退避してから適用します。'))
        bar = ttk.Progressbar(self.content, mode="indeterminate")
        bar.pack(fill="x")
        bar.start(14)
        language = self.language.get()
        addon, lbx64, vcache = (self.install_addon.get(), self.install_lbx64.get(), self.install_vcache.get())
        self.run(lambda: install(self.output, self.target, language=language, mw_reference=self.mw_reference,
                                 addon=addon, lbx64=lbx64, vcache=vcache),
                 lambda _: self.complete(installed=True),
                 lambda: self.complete(placement_failed=True))

    def complete(self, installed=False, placement_failed=False):
        self.page(4, tr('配置が完了しました') if installed else tr('変換済みパックを保存しました（未配置）'),
                  tr('Most Wanted の speed.exe から起動できます。') if installed
                  else tr('ゲームには反映されていません。この画面から配置できます。')
                  if self.can_install else tr('ゲームには反映されていません。導入先を再検査してから配置してください。'),
                  scroll=self.result is not None)
        if installed:
            self.card(tr('導入先'), str(self.target), tr('配置済み'))
        self.card(tr('ワールドパック'), f"{self.selected_quality}\n{self.output}", tr('検証済み'))
        if self.result:
            summary = self.result["resourceSummary"]
            if summary["carbonTextures"]:
                self.card(tr('Carbon 資源'), tr('空・照明・反射など {1} 種を補いました。', None, len(summary['carbonTextures'])), tr('使用済み'))
            if pursuit := summary["pursuit"]:
                detail = (tr('物理配置 {1}件、専用カメラ {3}シーンを生成。原本モデル欠落 {5}件。', None, format(pursuit['placements'], ','), None, pursuit['cameras'], None, pursuit['missingPlacements']))
                if recovered := pursuit.get("sourceAttributesRecovered"):
                    detail += tr('W2C原本から物理属性を{1}件補完。', None, len(recovered))
                self.card(tr('パースートブレーカーと小物'), detail, tr('生成済み'))
            self.card(tr('MWの料金所'), tr('本体・遮断機・標識など {1}件を復元。', None, summary['mwTolls']['placements']), tr('生成済み'))
            effects = summary["worldEffects"]
            lighting = summary["nightLighting"]
            self.card(tr('ワールドアニメーションと環境エフェクト'),
                      tr('動く構造物 {1}件、テクスチャアニメーション {3}件、環境エフェクト {5}件。\nMWの類似エフェクトで代替 {7}件。資源がなく省略 {9}件。', None, format(summary['worldMotions'], ','), None, format(lighting['textureAnimations'], ','), None, format(effects['placements'], ','), None, format(effects['mwSubstitutes'], ','), None, format(effects['omitted'], ',')),
                      tr('生成済み'))
            if effects["omittedGroups"]:
                self.card(tr('省略された環境エフェクト'),
                          ", ".join(effects["omittedGroups"]), tr('確認'), WARNING)
            self.card(tr('資源の省略'), tr('未解決の地形テクスチャ {1}種、モデルが見つからない配置 {3}件。\nMW資源で動く小物を {5}件復元。別の小物 {7}件は外観のみ再現。動的な当たり判定・破壊は {9}件が未変換です。', None, len(summary['missingTerrainTextures']), None, summary['missingStaticPlacements'], None, format(summary['mwFallbackProps'], ','), None, format(summary['visualPropPlacements'], ','), None, format(summary['unconvertedDynamicPropPlacements'], ',')), tr('確認'), WARNING)
            if pursuit and pursuit["omittedCameras"]:
                self.card(tr('パースートブレーカーと専用カメラ'), ", ".join(pursuit["omittedCameras"]), tr('未対応'), WARNING)
            if omission := summary["carbonPursuitOmission"]:
                self.card(tr('パースートブレーカーと専用カメラ'), omission, tr('確認'), WARNING)
            limits = summary["pendingFeatures"]
            if limits:
                self.card(tr('再現できていない機能'),
                          "\n".join(tr('・') + tr(FEATURE_LIMITATIONS.get(key, key)) for key in limits), tr('未対応'), WARNING)
        if installed:
            self.button(tr('導入先を開く'), lambda: os.startfile(self.target))
        self.button(tr('出力フォルダーを開く'), lambda: os.startfile(self.output))
        if placement_failed:
            self.button(tr('配置を再試行'), self.place, primary=True, footer=True)
        elif not installed and self.can_install:
            self.button(tr('このパックを配置'), self.place_exported, primary=True, footer=True)
        self.button(tr('入力へ戻る'), self.show_inputs, footer=True)
        self.button(tr('閉じる'), self.close, primary=installed or not self.can_install, footer=True)

    def run(self, task, success, failure, *, cancellable=False):
        if self.busy:
            return
        self.busy = True
        self.can_cancel = cancellable
        self.cancellation.clear()
        self.on_success = success
        self.on_failure = failure

        def work():
            try:
                self.events.put(("success", task()))
            except ConversionCancelled:
                self.events.put(("cancelled", None))
            except Exception as error:
                self.events.put(("error", str(error)))

        Thread(target=work, daemon=True).start()

    def poll(self):
        try:
            while True:
                event, value = self.events.get_nowait()
                if event == "progress":
                    if not self.cancellation.is_set():
                        self.stage_text.set(localized_detail(value[0]))
                        self.bar.configure(value=value[1])
                    continue
                self.busy = self.can_cancel = False
                if event == "success":
                    self.on_success(value)
                else:
                    if event == "error":
                        messagebox.showerror(tr('処理を完了できませんでした'), localized_detail(value), parent=self.root)
                    self.on_failure()
        except Empty:
            self.root.after(80, self.poll)

    def cancel(self):
        self.cancellation.set()
        self.cancel_button.configure(state="disabled")
        self.stage_text.set(tr('現在の処理が終わり次第、中止します。'))

    def close(self):
        if not self.busy:
            self.root.destroy()
        elif self.can_cancel:
            self.cancel()


def main():
    root = tk.Tk()
    ConverterWindow(root)
    root.mainloop()


if __name__ == "__main__":
    main()
