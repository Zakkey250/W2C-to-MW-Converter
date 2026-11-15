"""Update a W2C to MW 2.0.0 public RC4 installation to RC5."""
import argparse
import json
from pathlib import Path
from subprocess import CalledProcessError
import sys

from install import ASI, CONTROL, MANIFEST, commit_changes, installed_world, installer_lock
from install import json_bytes, recover, require_game_closed, sha, transaction_paths, under
from preflight import check_target, checked_folder

# Public RC4 executable shipped on GitHub. Other previews can use different
# world features even if the installer and ASI have the same file names.
RC4 = "76bb735bced9afdc46099a0d4aaa7adbb3cfe222f1511b8dbafcac28168a5220"
UPDATE = "1bae1ed1761ed2bb4920eca950cc05e7ae092adc21403e174b4894f58f56e3c6"
REBUILD = ("If the patch still fails, run Converter RC5 and rebuild from the original W2C 2.0.0 files.\n"
           "適用に失敗する場合は、コンバーター RC5 で W2C 2.0.0 の原本から再構築してください。")
PATCH_ERRORS = (OSError, ValueError, KeyError, CalledProcessError)


def update(mw, payload):
    if check_target(mw)["status"] != "ok":
        raise ValueError("Select an installation with a compatible Most Wanted 1.3 LAA speed.exe.")
    if sha(payload) != UPDATE:
        raise ValueError("The RC5 ASI is missing or its SHA-256 is incorrect.")
    require_game_closed()
    if not under(mw, MANIFEST).is_file():
        raise ValueError("No installed W2C to MW RC4 world was found in this folder.")
    with installer_lock(mw):
        journal_name, _, _ = transaction_paths(sunset=False)
        journal = under(mw, journal_name)
        if journal.is_file():
            rows = json.loads(journal.read_bytes())
            if (not isinstance(rows, list) or len(rows) != 2 or
                    not all(isinstance(row, dict) and isinstance(row.get("path"), str)
                            and {"before", "after"} <= row.keys() for row in rows) or
                    {r["path"] for r in rows} != {ASI, MANIFEST} or
                    not any(r["path"] == ASI and r["before"] == RC4 and r["after"] == UPDATE
                            for r in rows)):
                raise ValueError("Finish or recover the pending converter operation before applying this update.")
            recover(mw)
        if under(mw, transaction_paths(sunset=True)[0]).exists():
            raise ValueError("Finish or recover the pending SunSet operation first.")
        state = installed_world(mw)
        if state is None:
            raise ValueError("No installed W2C to MW RC4 world was found in this folder.")
        current = sha(under(mw, ASI))
        if current != state["runtimeSha256"] or current not in (RC4, UPDATE):
            raise ValueError("Select a game folder with the original public RC4 runtime and a matching installation record.")
        index = under(mw, f"{CONTROL}/generations/{state['generation']}/World/L5RA.w2i")
        if not state.get("indexSha256") or sha(index) != state["indexSha256"]:
            raise ValueError("The installed W2C world index does not match the installation record.")
        if current == UPDATE:
            return False
        updated = {**state, "runtimeSha256": UPDATE}
        commit_changes(mw, {ASI: payload, MANIFEST: json_bytes(updated)})
        return True


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--game", type=Path, help="folder to update")
    args.add_argument("--payload", type=Path, help=argparse.SUPPRESS)
    options = args.parse_args()
    home = Path(sys.executable).parent if getattr(sys, "frozen", False) else Path(__file__).parent
    payload = options.payload or home / ASI
    if options.game:
        try:
            print(json.dumps({"updated": update(checked_folder(options.game), payload)}))
        except PATCH_ERRORS as error:
            args.exit(2, f"{error}\n\n{REBUILD}\n")
        return

    import tkinter as tk
    from tkinter import filedialog, messagebox
    root = tk.Tk()
    root.withdraw()
    title = "W2C to MW 2.0.0 — RC4 to RC5 Patch"
    try:
        folder = filedialog.askdirectory(parent=root, title="Select your W2C game folder / ゲームフォルダーを選択")
        if folder and messagebox.askokcancel(title, f"Update this RC4 installation to RC5?\nRC5 に更新しますか？\n\n{folder}", parent=root):
            applied = update(checked_folder(folder), payload)
            messagebox.showinfo(title, "Updated to RC5. / RC5 に更新しました。" if applied
                                else "RC5 is already installed. / RC5 は適用済みです。", parent=root)
    except PATCH_ERRORS as error:
        messagebox.showerror(title, f"{error}\n\n{REBUILD}", parent=root)
    finally:
        root.destroy()


if __name__ == "__main__":
    main()
