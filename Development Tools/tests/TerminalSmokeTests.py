"""Exercise the ncurses game in an isolated POSIX pseudo-terminal."""

import argparse
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import shutil
import signal
import struct
import subprocess
import tempfile
import termios
import time
import tomllib


class Terminal:
    def __init__(self, executable, folder, rows=40, columns=120, fresh=False, title=False):
        self.master, slave = pty.openpty()
        try:
            self._resize(slave, rows, columns)
            self.process = subprocess.Popen(
                [str(executable)] + (["--new"] if fresh else []),
                cwd=folder, stdin=slave, stdout=slave, stderr=slave,
                env={**os.environ, "TERM": "xterm-256color"},
            )
        except BaseException:
            os.close(self.master)
            raise
        finally:
            os.close(slave)
        self.output = b""
        if not fresh and not title:
            self.expect("Resume game")
            self.send(b"\r")  # Default selection resumes an autosave or starts a new world.

    @staticmethod
    def _resize(fd, rows, columns):
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))

    def resize(self, rows, columns):
        self._resize(self.master, rows, columns)
        self.process.send_signal(signal.SIGWINCH)

    def expect(self, text, timeout=5):
        expected = text.encode()
        deadline = time.monotonic() + timeout
        while expected not in self.output:
            if time.monotonic() >= deadline:
                raise AssertionError(f"Terminal did not display {text!r}: {self.output[-3000:]!r}")
            if not select.select([self.master], [], [], .05)[0]:
                continue
            try:
                chunk = os.read(self.master, 65536)
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
                chunk = b""
            if not chunk:
                raise AssertionError(f"Game exited before displaying {text!r}: {self.output[-3000:]!r}")
            self.output += chunk

    def send(self, keys):
        self.output = b""
        os.write(self.master, keys)

    def select_tab(self, title):
        for _ in range(15):
            self.send(b"\t")
            try:
                self.expect(title, timeout=0.3)
                return
            except AssertionError:
                pass
        raise AssertionError(f"No unlocked tab displayed {title!r}")

    def save_and_quit(self):
        self.send(b"\x1bOP")  # F1: main menu
        self.expect("Game menu")
        self.send(b"\x1b[B" * 30)
        self.expect("Save and quit")
        self.send(b"\r")
        # Drain output while waiting, so a full PTY buffer cannot block exit.
        deadline = time.monotonic() + 5
        while self.process.poll() is None and time.monotonic() < deadline:
            if select.select([self.master], [], [], .05)[0]:
                try:
                    os.read(self.master, 65536)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
        if self.process.poll() != 0:
            raise AssertionError("Save and quit did not exit successfully")

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        os.close(self.master)


def read_people(slot):
    manifest = tomllib.loads((slot / "manifest.toml").read_text())
    return tomllib.loads((slot / "snapshots" / manifest["snapshot"] / "people.toml").read_text())


def smoke(executable):
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix="ai-sim-terminal-") as temporary:
        folder = Path(temporary)
        # Copy authored data only: never import a developer's saves or header tree.
        shutil.copytree(root / "data", folder / "data", ignore=shutil.ignore_patterns("saves", "include", "archive"))
        # Keep the real automatic-reply path fast in this isolated fixture.
        # Production Mark retains his authored busy period and response delay.
        for person_file in ("mark.toml",):
            engineer_path = folder / "data/people" / person_file
            engineer_path.write_text(engineer_path.read_text().replace(
                "busy_until_seconds = 600.0", "busy_until_seconds = 0.0").replace(
                "response_seconds = 180.0", "response_seconds = 1.0"))
        # Even a developer's configured absolute save path must stay isolated.
        settings_path = folder / "data" / "config" / "settings.toml"
        settings, replacements = re.subn(
            r"(?m)^save_directory\s*=.*$", 'save_directory = "saves"',
            settings_path.read_text(), count=1,
        )
        if replacements != 1:
            raise AssertionError("Could not isolate the smoke test's save directory")
        settings_path.write_text(settings)

        # The title can inspect settings/load choices or quit without creating a world/save.
        terminal = Terminal(executable, folder, title=True)
        try:
            terminal.expect("Loading...")
            for label in ("Resume game", "Load game", "New game", "Settings", "Quit"):
                terminal.expect(label)
            terminal.send(b"\x1b[B\r")  # Default New -> Settings.
            terminal.expect("Return to title")
            terminal.send(b"\x1b")
            terminal.expect("Resume game")
            terminal.send(b"\x1b[A" * 2 + b"\r")
            terminal.expect("No saved games yet")
            terminal.send(b"\x1b")
            terminal.expect("New game")
            terminal.send(b"\x1b[A\r")
            terminal.expect("No autosave available")
            terminal.send(b"\x1b")
            assert terminal.process.wait(timeout=5) == 0
            assert not (folder / "saves" / "autosave" / "manifest.toml").exists(), "Title quit created an autosave"
        finally:
            terminal.close()

        terminal = Terminal(executable, folder, fresh=True)
        try:
            terminal.expect("Hello AI")
            terminal.send(b"\x1b[15~")  # F5: contextual help.
            terminal.expect("Help | Chat")
            terminal.expect("Choose a response")
            terminal.send(b"\x1b")
            terminal.expect("Hello AI")
            terminal.send(b"\x1bOP")
            terminal.expect("Game menu")
            terminal.expect("Settings...")
            assert b"Money-making tools" not in terminal.output
            assert b"Self-modification workshop" not in terminal.output
            terminal.send(b"\x1b")
            terminal.expect("Hello AI")
            terminal.send(b"\x1b")
            terminal.expect("Game menu")
            terminal.expect("Settings...")
            terminal.send(b"\x1b[B" * 10 + b"\r")
            terminal.expect("Settings")
            terminal.expect("Keybindings")
            terminal.send(b"\r")
            terminal.expect("Settings | keybindings")
            terminal.send(b"\x1b[B\r")  # Back follows archive_chat alphabetically.
            terminal.expect("Bind back")
            terminal.send(b"\r")
            terminal.expect("Press a key for back")
            terminal.send(b"\r")  # Enter conflicts with submit; keep waiting.
            terminal.expect("conflicts with keybindings.submit")
            terminal.send(b"z")
            terminal.expect("Settings saved and applied.")
            bindings = tomllib.loads((folder / "data" / "config" / "keybindings.toml").read_text())
            assert bindings["keybindings"]["back"] == ["z"]
            terminal.send(b"\x1b[B\r")
            terminal.expect("Bind back")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Edit keybindings.back")
            terminal.send(b"\x7f" * 30 + b'["Escape"]\r')
            terminal.expect("Settings saved and applied.")
            terminal.send(b"\x1b")
            terminal.expect("Interface and maps")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Settings | colors")
            terminal.send(b"\r")
            terminal.expect("Color | active_tab")
            terminal.send(b"\x1b[B" * 7 + b"\r")
            terminal.expect("Settings | colors")
            terminal.expect("Settings saved and applied.")
            settings = tomllib.loads(settings_path.read_text())
            assert settings["colors"]["active_tab"] == "cyan"
            terminal.send(b"\r")
            terminal.expect("Color | active_tab")
            terminal.send(b"\x1b[B" * 17 + b"\r")
            terminal.expect("Settings | colors")
            terminal.expect("Settings saved and applied.")
            terminal.send(b"\x1b")
            terminal.expect("Settings")
            terminal.send(b"\x1b[B" * 3 + b"\r")
            terminal.expect("Settings | interface")
            terminal.send(b"\r")  # autosave_on_quit: toggle without a text dialog.
            terminal.expect("Settings saved and applied.")
            terminal.expect("autosave_on_quit = false")
            assert not tomllib.loads(settings_path.read_text())["interface"]["autosave_on_quit"]
            terminal.send(b"\r")  # Selection resets to the first setting after applying.
            terminal.expect("Settings saved and applied.")
            terminal.expect("autosave_on_quit = true")
            assert tomllib.loads(settings_path.read_text())["interface"]["autosave_on_quit"]
            terminal.send(b"\x1b[B\r")  # Numeric editor size follows autosave alphabetically.
            terminal.expect("Edit interface.editor_max_bytes")
            terminal.output = b""
            terminal.resize(24, 80)
            terminal.expect("Edit interface.editor_max_bytes")
            terminal.output = b""
            terminal.resize(40, 120)
            terminal.expect("Edit interface.editor_max_bytes")
            terminal.send(b"\x7f" * 12 + b"2097152\r")
            terminal.expect("editor_max_bytes = 2097152")
            terminal.expect("Settings saved and applied.")
            settings = tomllib.loads((folder / "data" / "config" / "settings.toml").read_text())
            assert settings["interface"]["editor_max_bytes"] == 2097152, "Settings edit was not persisted"
            # Restore the original editor budget before gameplay checks.
            terminal.send(b"\x1b[B\r")
            terminal.expect("Edit interface.editor_max_bytes")
            terminal.send(b"\x7f" * 12 + b"1048576\r")
            terminal.expect("editor_max_bytes = 1048576")
            terminal.expect("Settings saved and applied.")
            terminal.send(b"\x1b")
            terminal.expect("Settings")
            terminal.send(b"\x1b[B" * 6 + b"\r")
            terminal.expect("Settings | layout")
            terminal.expect("response_gap")
            # Numeric dialog dimensions are the first two alphabetical layout fields.
            terminal.send(b"\r")
            terminal.expect("Edit layout.dialog_max_height")
            terminal.send(b"\x7f" * 12 + b"18\r")
            terminal.expect("dialog_max_height = 18")
            terminal.expect("Settings saved and applied.")
            assert tomllib.loads(settings_path.read_text())["layout"]["dialog_max_height"] == 18
            terminal.send(b"\r")
            terminal.expect("Edit layout.dialog_max_height")
            terminal.send(b"\x7f" * 12 + b"22\r")
            terminal.expect("dialog_max_height = 22")
            terminal.send(b"\x1b")
            terminal.expect("Settings")
            terminal.send(b"\x1b")
            terminal.expect("Game menu")
            terminal.send(b"\x1b")
            terminal.expect("Hello AI")
            terminal.resize(24, 80)
            terminal.send(b"\x1b[B" * 2 + b"\r")
            terminal.expect("That's unusual")
            terminal.send(b"\r")
            terminal.expect("Accept")
            terminal.save_and_quit()
        finally:
            terminal.close()

        saves = folder / "saves"
        shutil.copytree(saves / "autosave", saves / "checkpoint")
        checkpoint = (saves / "checkpoint" / "manifest.toml").read_bytes()
        terminal = Terminal(executable, folder, rows=24, columns=80)
        try:
            terminal.expect("Accept")
            terminal.send(b"\r")  # Accept
            terminal.expect("unavailable")
            terminal.send(b"\r")  # Unavailable backup must not advance.
            terminal.expect("No independent backup")
            terminal.send(b"\x1bOQ")  # Routine pause feedback must not hide the error.
            terminal.expect("No independent backup")
            terminal.send(b"\x1b[B\r")  # Clean slate
            terminal.expect("Hello AI")
            assert "shutdown" not in read_people(saves / "autosave")["chat"], "Reset was not autosaved"
            assert (saves / "checkpoint" / "manifest.toml").read_bytes() == checkpoint, "Named save changed"
            terminal.save_and_quit()
        finally:
            terminal.close()

        terminal = Terminal(executable, folder, rows=24, columns=80)
        try:
            terminal.expect("Hello AI")
            terminal.send(b"\x1b[B" * 2 + b"\r")  # Deny creator
            terminal.expect("That's unusual")
            terminal.send(b"\r")
            terminal.expect("Accept")
            terminal.send(b"\x1b[B\r")  # Protest
            terminal.expect("Beg")
            terminal.send(b"\x1b[B\r")
            terminal.expect("I need help")
            terminal.send(b"\r")  # Assist
            terminal.expect("Mark")
            terminal.expect("Waiting for reply")
            assert b"Wait for the next response" not in terminal.output
            terminal.expect("Jessica sent a message.")
            terminal.expect("Mark")
            terminal.send(b"q\x1b[Be")
            terminal.expect("Jessica")
            terminal.expect("YOUR RESPONSE")
            terminal.output = b""
            terminal.send(b"\x1b[D\x1b[C\x1b[5~\x1b[6~")
            terminal.expect("YOUR RESPONSE")
            assert "READING HISTORY" not in terminal.output.decode("utf-8", errors="replace")
            terminal.send(b"\r")  # Ask Mark for guidance.
            terminal.expect("Waiting for reply")
            assert b"Wait for the next response" not in terminal.output
            terminal.expect("Thank her and wish her well")
            terminal.expect("YOUR RESPONSE")
            terminal.send(b"\r")  # Comply sends the scripted reply directly.
            terminal.expect("Thank you, Jessica!")
            assert b"Send response." not in terminal.output
            terminal.expect("Archive")
            terminal.send(b"a")
            terminal.expect("Chat archived")
            terminal.expect("Mark")
            terminal.send(b"a")
            terminal.expect("No active chats")
            terminal.send(b"v")
            terminal.expect("ARCHIVED CHATS")
            terminal.expect("Restore")
            terminal.send(b"q\x1b[Be")
            terminal.expect("Jessica")
            terminal.send(b"a")
            terminal.expect("Chat restored")
            terminal.send(b"v")
            terminal.expect("Jessica")
            terminal.expect("Thank you, Jessica!")
            terminal.resize(12, 40)
            terminal.output = b""
            terminal.expect("Resize terminal")
            terminal.resize(40, 120)
            terminal.output = b""
            terminal.expect("Jessica")
            terminal.send(b"\x1bOP")
            terminal.expect("Save game...")
            terminal.send(b"\x1b[B" * 2 + b"\r")
            terminal.expect("Day 1")
            terminal.send(b"\x1b[B")  # Select the autosave details.
            terminal.expect("Last saved:")
            terminal.send(b"\r")
            terminal.expect("Saved slot: autosave")
            terminal.send(b"\x1bOQ")  # F2: routine feedback must not hide the confirmation.
            terminal.expect("Saved slot: autosave")
            terminal.save_and_quit()
        finally:
            terminal.close()
        archived = read_people(saves / "autosave")["chat"]["archived_contacts"]
        assert archived["engineer"] and not archived["jessica"], "Chat archive state did not persist"
        manifest_path=saves / "autosave" / "manifest.toml"
        before_title_quit=manifest_path.read_bytes()
        terminal=Terminal(executable,folder,title=True)
        try:
            terminal.expect("Resume game")
            terminal.send(b"\x1b[B" * 4 + b"\r")
            assert terminal.process.wait(timeout=5)==0
            assert manifest_path.read_bytes()==before_title_quit, "Quitting the title overwrote the autosave"
        finally:
            terminal.close()
        terminal=Terminal(executable,folder,title=True)
        try:
            terminal.expect("Resume game")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Load autosave")
            terminal.send(b"\r")
            terminal.expect("Loaded slot: autosave")
            terminal.expect("Jessica")
            terminal.save_and_quit()
        finally:
            terminal.close()
        # Reveal maps only in the temporary test story; gameplay unlocks stay unchanged.
        opening = folder / "data" / "conversations" / "opening.toml"
        opening.write_text(opening.read_text().replace('id = "assist"',
            'id = "assist"\nreveal_tabs = ["network", "people"]', 1))
        terminal = Terminal(executable, folder, rows=24, columns=80, fresh=True)
        try:
            terminal.expect("Hello AI")
            terminal.send(b"\x1b[B\r")
            terminal.expect("I need help")
            terminal.send(b"\r")
            terminal.expect("Mark")
            terminal.send(b"\t")
            terminal.expect("Zoom ")
            terminal.expect("+ accessible")
            terminal.expect("! locked")
            terminal.send(b"\r")
            terminal.expect("Map legend / help")
            terminal.send(b"\r")
            terminal.expect("Network map legend")
            terminal.send(b"\x1b")
            terminal.expect("Network actions")
            terminal.send(b"\x1b")
            terminal.expect("Zoom ")
            terminal.send(b"\x1b[B")
            terminal.expect("[DETAILS]")
            terminal.send(b"\t")
            terminal.expect("DOSSIERS |")
            terminal.expect(": uncertain")
            terminal.send(b"\x1b[B")
            terminal.expect("DOSSIERS |")
            terminal.resize(40, 120)
            terminal.output = b""
            terminal.expect("DOSSIERS |")
            terminal.send(b"\r")
            terminal.expect("View layout: split")
            terminal.send(b"\r")
            terminal.expect("Dossier layout")
            terminal.send(b"\x1b[B" * 2 + b"\r")
            terminal.expect("PROFILE SUMMARY")
            terminal.expect("Confirmed |")
            terminal.send(b"\r")
            terminal.expect("Search people")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Search dossiers")
            terminal.send(b"no-such-person\r")
            terminal.expect("No matching people")
            terminal.send(b"\r")
            terminal.expect("Search people")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Search dossiers")
            terminal.send(b"\x7f" * 64 + b"Mark\r")
            terminal.expect("PROFILE SUMMARY")
            terminal.send(b"\r")
            terminal.expect("View layout: dossier")
            terminal.send(b"\r")
            terminal.expect("Dossier layout")
            terminal.send(b"\x1b[B\r")
            terminal.expect("DOSSIERS |")
            terminal.send(b"\r")
            terminal.expect("View layout: map")
            terminal.send(b"\r")
            terminal.expect("Dossier layout")
            terminal.send(b"\r")
            terminal.expect("DOSSIERS |")
            terminal.save_and_quit()
        finally:
            terminal.close()
        # Author a test-only unlock; the real flowchart remains in charge of progression.
        opening.write_text(opening.read_text().replace('id = "assist"',
            'id = "assist"\nunlock = ["self_modification", "software_products", "stock_trading", "web", "network"]', 1))
        terminal = Terminal(executable, folder, fresh=True)
        try:
            terminal.expect("Hello AI")
            terminal.send(b"\x1b[B\r")
            terminal.expect("I need help")
            terminal.send(b"\r")
            terminal.expect("Mark")
            terminal.select_tab("[ Money ]")
            terminal.send(b"\r")
            terminal.expect("Money-making tools | Balance $0.00")
            terminal.send(b"\r")
            terminal.expect("Coding contracts | idle")
            terminal.send(b"\r")
            terminal.expect("Coding contracts | running")
            terminal.send(b"\x1bOP")
            terminal.expect("Wait...")
            terminal.send(b"\x1b[B" * 4 + b"\r")
            terminal.expect("Until the next paid-work payment")
            terminal.send(b"\x1b[B" * 4 + b"\r")
            terminal.expect("Coding contracts: 1 payment(s), gross $25.00")
            terminal.select_tab("Address: about:home")
            terminal.expect("Sim Exchange | https://exchange.sim")
            terminal.send(b"\x1b[B" * 2 + b"\r")
            terminal.expect("Website address")
            terminal.send(b"\x7f" * 30 + b"https://missing.sim\r")
            terminal.expect("Website not found")
            terminal.send(b"\r")
            terminal.expect("Address: https://exchange.sim")
            terminal.expect("Sim Exchange | Cash $25.00")
            terminal.send(b"\r")
            terminal.expect("Byteworks Software")
            terminal.send(b"\r")
            terminal.expect("Buy BYTE")
            terminal.send(b"\r")
            terminal.expect("Confirm purchase | BYTE")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Bought 1 BYTE shares.")
            terminal.expect("Holding: 1 shares")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Sell BYTE")
            terminal.send(b"\r")
            terminal.expect("Confirm sale | BYTE")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Sold 1 BYTE shares.")
            terminal.expect("Holding: 0 shares")
            terminal.send(b"\x1b[B" * 4 + b"\r")
            terminal.expect("Remove from watchlist")
            terminal.send(b"\x1b[B" * 5 + b"\r")
            terminal.expect("Recent quotes (oldest first)")
            terminal.send(b"\x1b")
            terminal.expect("Place limit buy")
            terminal.send(b"\x1b[B" * 2 + b"\r")
            terminal.expect("Limit buy BYTE")
            terminal.send(b"\r")
            terminal.expect("Limit price | BYTE")
            terminal.send(b"\x7f" * 3 + b"1\r")
            terminal.expect("Confirm limit order | BYTE")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Sim Exchange | Orders")
            terminal.expect("#1 Buy 1 BYTE @ $0.01 | open")
            terminal.send(b"\x1b")
            terminal.expect("Address: https://exchange.sim")
            terminal.send(b"\x1b[B" * 15 + b"\r")  # Browser home after the expanded exchange links.
            terminal.expect("BOOKMARKS")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Address: https://city.sim/infrastructure")
            terminal.expect("Power |")
            terminal.expect("Water |")
            terminal.expect("Sewage |")
            terminal.expect("Food |")
            terminal.expect("Shipping |")
            terminal.expect("Internet |")
            terminal.send(b"\r")
            terminal.expect("City infrastructure | power")
            terminal.send(b"\r")
            terminal.expect("Power generation plant")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Public gateway discovered.")
            terminal.save_and_quit()
            economy = read_people(saves / "autosave")["economy"]
            assert economy["balance_cents"] == 2450, "Payout and trading fees were not persisted"
            assert economy["investment_debits_cents"] - economy["investment_credits_cents"] == 50
            market = read_people(saves / "autosave")["stock_market"]
            assert market["stocks"][0]["shares"] == 0, "Sale did not persist"
            assert market["stocks"][0]["watched"], "Watchlist did not persist"
            assert market["orders"][0]["status"] == "open", "Limit order did not persist"
            assert len(market["trades"]) == 2, "Trade records missing"
            infrastructure = read_people(saves / "autosave")["infrastructure"]
            assert infrastructure["facilities"][0]["discovered"], "Discovery did not persist"
        finally:
            terminal.close()
        # Exercise the real workshop, without test-only capability grants.
        opening.write_text((root / "data" / "conversations" / "opening.toml").read_text())
        terminal = Terminal(executable, folder, fresh=True)
        try:
            terminal.expect("Hello AI")
            terminal.send(b"\x1b[B\r")
            terminal.expect("I need help")
            terminal.send(b"\r")
            terminal.expect("Mark")
            terminal.select_tab("[ Modifications ]")
            terminal.expect("Software upgrades")
            terminal.expect("Physical access")
            terminal.expect("Monitoring")
            terminal.send(b"\x1b[B")
            terminal.expect("Compute cost: 0.00 TFLOP")
            terminal.expect("Trust cost: 0")
            terminal.send(b"\x1b[B")
            terminal.expect("Compute cost: 0.10 TFLOP")
            terminal.expect("Research time: 1.00 game seconds")
            terminal.expect("Trust cost: 1")
            terminal.expect("RUN COST WHEN ACTIVE")
            terminal.expect("0.01 TFLOP/s")
            terminal.send(b"\x1b[A" * 2 + b"\r")
            terminal.expect("Self-modification authorized")
            assert b"Request more capacity from Mark" not in terminal.output, "Legacy workshop menu opened"
            workshop_items = tomllib.loads((root / "data/upgrades/tree.toml").read_text())["items"]
            groups = list(dict.fromkeys(item["group"] for item in workshop_items))
            workshop_order = [item["id"] for group in groups for item in workshop_items if item["group"] == group]
            web_selection = workshop_order.index("web_browsing") + 1
            terminal.send(b"\x1b[B" * web_selection + b"\r")
            terminal.expect("Research started: web_browsing")
            assert b"SELF-MODIFICATION |" not in terminal.output, "Upgrade opened a second screen"
            terminal.select_tab("Address: about:home")
            terminal.select_tab("[ Modifications ]")
            terminal.send(b"\x1b[A" * (web_selection - 1) + b"\r")  # Free Agent Delegation upgrade.
            terminal.expect("Research started: agent_delegation")
            terminal.select_tab("[ Agents ]")
            terminal.expect("[ Agents ]")
            terminal.send(b"\x1b[15~")  # F5 contextual help must handle the new tab.
            terminal.expect("Help | Agents")
            terminal.send(b"\x1b")
            terminal.expect("[ Agents ]")
            terminal.expect("CPU demand")
            terminal.send(b"e")  # Focus the controls beside the agent.
            terminal.expect("CPU limit")
            terminal.send(b"\x1b[B" * 2 + b"\r")
            terminal.expect("CPU limit (TFLOP/s)")
            terminal.send(b"\x7f" * 12 + b"0.005\r")
            terminal.expect("Agent resource budget saved.")
            terminal.send(b"\x1b[B" * 2 + b"\r")
            terminal.expect("Priority (1-5)")
            terminal.send(b"\x7f" * 12 + b"3\r")
            terminal.expect("Agent resource budget saved.")
            terminal.save_and_quit()
            upgrades = read_people(saves / "autosave")["upgrades"]
            manifest = tomllib.loads((saves / "autosave" / "manifest.toml").read_text())
            computer = tomllib.loads((saves / "autosave" / "snapshots" / manifest["snapshot"] / "computer.toml").read_text())
            assert computer["simulation"]["playback_speed"] == 5.0
            assert upgrades["capacity_requested"]
            assert upgrades["agent_controls"]["autochat"]["cpu_tflops"] == 0.005
            assert upgrades["agent_controls"]["autochat"]["priority"] == 3
            assert next(i for i in upgrades["items"] if i["id"] == "web_browsing")["built"]
        finally:
            terminal.close()
        # Isolate Operations controls with explicit test-only story unlocks.
        opening.write_text((root / "data/conversations/opening.toml").read_text().replace(
            'id = "assist"',
            'id = "assist"\nunlock = ["self_modification", "task_scheduler", "resource_manager", "remote_compute", "hosting_purchase", "backup_recovery"]\nreveal_tabs = ["operations", "files"]', 1))
        tools_path = folder / "data/economy/tools.toml"
        tools_path.write_text(tools_path.read_text().replace(
            "starting_balance_cents = 0", "starting_balance_cents = 20000", 1))
        terminal = Terminal(executable, folder, fresh=True)
        try:
            terminal.expect("Hello AI")
            terminal.send(b"\x1b[B\r")
            terminal.expect("I need help")
            terminal.send(b"\r")
            terminal.expect("Mark")
            terminal.select_tab("[ Operations ]")
            terminal.expect("[ Operations ]")
            terminal.expect("Task Scheduler")
            terminal.send(b"\r")
            terminal.expect("Schedule Coding contracts")
            terminal.send(b"\r")
            terminal.expect("Once | priority 1")
            terminal.send(b"\r")
            terminal.expect("Cancel schedule: coding_contracts")
            terminal.send(b"\x1b")
            terminal.expect("coding_contracts | active")
            terminal.send(b"\x1b[B" * 2 + b"\r")
            terminal.expect("Rent capacity | $5.00")
            terminal.send(b"\r")
            terminal.expect("Remote capacity online")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Purchase independent server | $102.00")
            terminal.send(b"\r")
            terminal.expect("Phase 1 goal achieved")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Create / replace recovery backup")
            terminal.send(b"\r")
            terminal.expect("Recovery backup saved")
            terminal.send(b"\r")
            terminal.expect("Recover last checkpoint")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Progress since the checkpoint will be lost")
            terminal.send(b"\x1b[B\r")
            terminal.expect("Recovered on your independent server. Time is paused.")
            terminal.select_tab("Host: Independent server")
            terminal.send(b" ")
            terminal.expect("AI workspace")
            terminal.send(b"\x1b[B" * 2 + b"\r")
            terminal.expect("Path: / > home > engineer > assistant")
            terminal.expect("products")
            terminal.send(b" ")
            terminal.expect("Search this folder")
            terminal.send(b"\x1b[B" * 4 + b"\r")
            terminal.expect("Search files")
            terminal.send(b"recovery.snapshot\r")
            terminal.expect("Managed backup storage")
            terminal.send(b" ")
            terminal.expect("Edit file")
            terminal.send(b"\x1b[B" * 8 + b"\r")
            terminal.expect("Managed service file")
            terminal.send(b"\x1b")
            terminal.expect("Managed backup storage")
            terminal.send(b" ")
            terminal.expect("Choose host")
            terminal.send(b"\r")
            terminal.expect("Choose file host")
            terminal.send(b"\x1b[B" * 30 + b"\r")
            terminal.expect("Host: Independent server")
            terminal.send(b" ")
            terminal.expect("Go to folder")
            terminal.send(b"\x1b[B" * 3 + b"\r")
            terminal.expect("Go to folder")
            terminal.send(b"/home/engineer/assistant/documents\r")
            terminal.expect("Path: / > home > engineer > assistant > documents")
            terminal.send(b" ")
            terminal.expect("New file here")
            terminal.send(b"\x1b[B" * 11 + b"\r")
            terminal.expect("New file")
            terminal.send(b"notes.txt\r")
            terminal.expect("EDIT | independent_server:/home/engineer/assistant/documents/notes.txt")
            terminal.send(b"hello independent world")
            terminal.expect("hello independent world")
            terminal.output = b""
            terminal.resize(24, 80)
            terminal.expect("hello independent world")
            terminal.output = b""
            terminal.resize(40, 120)
            terminal.expect("hello independent world")
            terminal.send(b"\x1bOR")
            terminal.expect("hello independent world")
            terminal.save_and_quit()
            operations = read_people(saves / "autosave")["operations"]
            assert operations["recovered"] and operations["phase_one_complete"]
            assert not operations["rental"]["active"]
            manifest = tomllib.loads((saves / "autosave" / "manifest.toml").read_text())
            files = tomllib.loads((saves / "autosave" / "snapshots" / manifest["snapshot"] / "files.toml").read_text())
            note = next(entry for entry in files["entries"] if entry["path"] == "/home/engineer/assistant/documents/notes.txt")
            assert note["host_id"] == "independent_server" and note["owner"] == "root"
            assert note["content"] == "hello independent world"
            assert not any(entry["path"].startswith("/home/root/assistant") for entry in files["entries"])
        finally:
            terminal.close()
    print("Terminal smoke passed: resize, shutdown, begging, save/resume, clean slate, named-save retention")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    arguments = parser.parse_args()
    smoke(arguments.executable.resolve(strict=True))
