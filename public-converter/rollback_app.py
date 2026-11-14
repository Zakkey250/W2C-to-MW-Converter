"""Standalone confirmation and recovery for the installation containing this executable."""

from pathlib import Path
import sys
import tkinter as tk
from tkinter import messagebox, ttk

from converter_locale import LANGUAGES, detail, locale_label, set_locale_label, tr
from install import CONTROL, installed_world, uninstall, under


def main():
    root = tk.Tk()
    root.title(tr("W2C to MW のアンインストール"))
    root.minsize(460, 230)
    root.resizable(False, False)
    executable = Path(sys.executable)
    mw = executable.parent.parent
    state = None
    valid_location = (getattr(sys, "frozen", False) and executable.parent.name.casefold() == CONTROL.casefold()
                      and executable.resolve() == executable and mw.resolve() == mw)
    if valid_location:
        try:
            valid_location = under(mw, "speed.exe").is_file()
            if valid_location:
                state = installed_world(mw)
        except (OSError, ValueError) as error:
            root.withdraw()
            messagebox.showerror(root.title(), detail(str(error)), parent=root)
            root.destroy()
            return
    if state and state.get("language") in LANGUAGES:
        set_locale_label(state["language"])
    body = ttk.Frame(root, padding=20)
    body.pack(fill="both", expand=True)
    selection = ttk.Combobox(body, values=LANGUAGES, state="readonly", width=23)
    selection.set(locale_label())
    selection.pack(anchor="ne")
    headline = ttk.Label(body, font=("Segoe UI", 12, "bold"), wraplength=420)
    headline.pack(fill="x", pady=(15, 6))
    label = ttk.Label(body, text=str(mw) if valid_location else "", wraplength=420)
    label.pack(fill="x")
    description = ttk.Label(body, wraplength=420, justify="left")
    description.pack(fill="x", pady=(12, 20))
    buttons = ttk.Frame(body)
    buttons.pack(anchor="e")
    close = ttk.Button(buttons, command=root.destroy)
    close.pack(side="right")

    def restore():
        if not messagebox.askyesno(root.title(), tr("このフォルダーをMW原本の状態に戻しますか？"), parent=root):
            return
        try:
            restored = uninstall(mw)
        except (OSError, ValueError) as error:
            messagebox.showerror(root.title(), detail(str(error)), parent=root)
            return
        restore_button.configure(state="disabled")
        messagebox.showinfo(root.title(), tr("MW原本のワールドに戻しました。" if restored else
                                               "このフォルダーに有効なW2Cの配置はありません。"), parent=root)
        root.destroy()

    restore_button = ttk.Button(buttons, command=restore)
    restore_button.pack(side="right", padx=(0, 10))
    if not valid_location:
        restore_button.configure(state="disabled")

    def localize(_event=None):
        set_locale_label(selection.get())
        root.title(tr("W2C to MW のアンインストール"))
        headline.configure(text=tr("MW原本のワールドに戻す"))
        description.configure(text=tr("ゲームを終了してください。MW原本を復元し、W2Cを無効にします。セーブ、変換済みパック、SunSetとTexWizardは保持します。"
                                      if valid_location else "ゲームフォルダー内のW2CMWPublicから起動してください。"))
        restore_button.configure(text=tr("復元する"))
        close.configure(text=tr("閉じる"))

    selection.bind("<<ComboboxSelected>>", localize)
    localize()
    root.mainloop()


if __name__ == "__main__":
    main()
