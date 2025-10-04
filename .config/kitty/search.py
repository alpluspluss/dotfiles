# Kitty search kitten - optimized version
# Original: https://github.com/trygveaa/kitty-kitten-search
# License: GPLv3

import json
import re
import subprocess
import time
from gettext import gettext as _
from pathlib import Path
from subprocess import PIPE, run
from typing import Optional

from kittens.tui.handler import Handler
from kittens.tui.line_edit import LineEdit
from kittens.tui.loop import Loop
from kittens.tui.operations import (
    clear_screen,
    cursor,
    set_line_wrapping,
    set_window_title,
    styled,
)
from kitty.config import cached_values_for
from kitty.key_encoding import EventType
from kitty.typing_compat import KeyEventType, ScreenSize

# ============================================================================
# Regular expression patterns for text navigation
# ============================================================================
# Whitespace patterns for word-level navigation
SPACE_PATTERN = re.compile(r"\s+")
SPACE_PATTERN_END = re.compile(r"\s+$")
SPACE_PATTERN_START = re.compile(r"^\s+")

# Alphanumeric patterns for token-level navigation
NON_ALPHANUM_PATTERN = re.compile(r"[^\w\d]+")
NON_ALPHANUM_PATTERN_END = re.compile(r"[^\w\d]+$")
NON_ALPHANUM_PATTERN_START = re.compile(r"^[^\w\d]+")

# Path to the scroll-mark kitten for navigation between matches
SCROLLMARK_FILE = Path(__file__).parent.absolute() / "scroll-mark.py"


def call_remote_control(args: list[str]) -> None:
    """
    Execute a kitty remote control command.

    Remote control allows this kitten to manipulate the main kitty instance,
    such as creating markers for search matches or scrolling windows.

    Args:
        args: Command arguments to pass to 'kitty @'
    """
    subprocess.run(["kitty", "@", *args], capture_output=True)


def reindex(
        text: str, pattern: re.Pattern[str], right: bool = False
) -> tuple[int, int]:
    """
    Find the span of a pattern match in text, searching from left or right.

    Args:
        text: The text to search within
        pattern: Compiled regex pattern to match
        right: If True, find the rightmost match; otherwise find leftmost

    Returns:
        Tuple of (start_index, end_index) for the match span

    Raises:
        ValueError: If no match is found
    """
    if not right:
        m = pattern.search(text)
    else:
        # Find all matches and take the last one
        matches = list(pattern.finditer(text))
        if not matches:
            raise ValueError("No pattern match found")
        m = matches[-1]

    if not m:
        raise ValueError("No pattern match found")

    return m.span()


class Search(Handler):
    """
    Interactive search handler for kitty terminal scrollback.

    Provides incremental search with regex support, highlighted matches,
    and navigation between results. Supports both text and regex modes.
    """

    # Debounce interval to avoid excessive subprocess calls during typing
    MARK_DEBOUNCE_INTERVAL = 0.05  # 50ms

    def __init__(
            self, cached_values: dict[str, str], window_ids: list[int], error: str = ""
    ) -> None:
        """
        Initialize the search handler.

        Args:
            cached_values: Persistent values (last search, mode) from kitty config
            window_ids: List of window IDs to search within
            error: Optional error message to display
        """
        self.cached_values = cached_values
        self.window_ids = window_ids
        self.error = error

        # Initialize line editor with previous search term
        self.line_edit = LineEdit()
        last_search = cached_values.get("last_search", "")
        self.line_edit.add_text(last_search)

        # Mark text as selected if we restored a previous search
        self.text_marked = bool(last_search)

        # Search mode: "text" for literal search, "regex" for pattern matching
        self.mode = cached_values.get("mode", "text")
        self.update_prompt()

        # Debouncing state to reduce subprocess overhead
        self._last_mark_text = ""
        self._last_mark_time = 0.0

        # Apply initial search highlighting
        self.mark()

    def update_prompt(self) -> None:
        """Update the prompt string based on current search mode."""
        self.prompt = "~> " if self.mode == "regex" else "=> "

    def init_terminal_state(self) -> None:
        """Configure terminal settings for the search UI."""
        self.write(set_line_wrapping(False))
        self.write(set_window_title(_("Search")))

    def initialize(self) -> None:
        """Initialize the search interface on startup."""
        self.init_terminal_state()
        self.draw_screen()

    def draw_screen(self) -> None:
        """Render the search interface to the terminal."""
        self.write(clear_screen())

        if self.window_ids:
            input_text = self.line_edit.current_input

            # Highlight the input if text is marked (e.g., after restoration)
            if self.text_marked:
                self.line_edit.current_input = styled(input_text, reverse=True)

            self.line_edit.write(self.write, self.prompt)
            self.line_edit.current_input = input_text

        # Display any error messages
        if self.error:
            with cursor(self.write):
                self.print("")
                for line in self.error.split("\n"):
                    self.print(line)

    def refresh(self) -> None:
        """Redraw the screen and update search markers."""
        self.draw_screen()
        self.mark()

    def switch_mode(self) -> None:
        """Toggle between text and regex search modes."""
        self.mode = "regex" if self.mode == "text" else "text"
        self.cached_values["mode"] = self.mode
        self.update_prompt()

    def on_text(self, text: str, in_bracketed_paste: bool = False) -> None:
        """
        Handle text input events.

        Args:
            text: The input text
            in_bracketed_paste: Whether this is part of a paste operation
        """
        # Clear selection on new input
        if self.text_marked:
            self.text_marked = False
            self.line_edit.clear()

        self.line_edit.on_text(text, in_bracketed_paste)
        self.refresh()

    def on_key(self, key_event: KeyEventType) -> None:
        """
        Handle keyboard events for editing and navigation.

        Supports Emacs-style keybindings and custom navigation commands.

        Args:
            key_event: The keyboard event to handle
        """
        # Clear text marking on most key presses (except modifiers)
        if self._should_clear_mark(key_event):
            self.text_marked = False
            self.refresh()

        # Let LineEdit handle basic cursor movement and editing
        if self.line_edit.on_key(key_event):
            self.refresh()
            return

        # Custom keybindings
        if key_event.matches("ctrl+u"):
            # Clear entire line
            self.line_edit.clear()
            self.refresh()
        elif key_event.matches("ctrl+a"):
            # Move to start of line
            self.line_edit.home()
            self.refresh()
        elif key_event.matches("ctrl+e"):
            # Move to end of line
            self.line_edit.end()
            self.refresh()
        elif key_event.matches("ctrl+backspace") or key_event.matches("ctrl+w"):
            # Delete previous word (whitespace-delimited)
            self._delete_word(use_whitespace=True)
        elif key_event.matches("ctrl+left") or key_event.matches("ctrl+b"):
            # Move left by word (whitespace-delimited)
            self._move_word_left(use_whitespace=True)
        elif key_event.matches("ctrl+right") or key_event.matches("ctrl+f"):
            # Move right by word (whitespace-delimited)
            self._move_word_right(use_whitespace=True)
        elif key_event.matches("alt+backspace") or key_event.matches("alt+w"):
            # Delete previous token (alphanumeric-delimited)
            self._delete_word(use_whitespace=False)
        elif key_event.matches("alt+left") or key_event.matches("alt+b"):
            # Move left by token (alphanumeric-delimited)
            self._move_word_left(use_whitespace=False)
        elif key_event.matches("alt+right") or key_event.matches("alt+f"):
            # Move right by token (alphanumeric-delimited)
            self._move_word_right(use_whitespace=False)
        elif key_event.matches("tab"):
            # Toggle between text and regex mode
            self.switch_mode()
            self.refresh()
        elif key_event.matches("up") or key_event.matches("f3"):
            # Navigate to previous match
            self._navigate_to_match(forward=False)
        elif key_event.matches("down") or key_event.matches("shift+f3"):
            # Navigate to next match
            self._navigate_to_match(forward=True)
        elif key_event.matches("enter"):
            # Accept search and close
            self.quit(0)
        elif key_event.matches("esc"):
            # Cancel search and close
            self.quit(1)

    def _should_clear_mark(self, key_event: KeyEventType) -> bool:
        """
        Determine if a key event should clear the text marking.

        Args:
            key_event: The key event to check

        Returns:
            True if the mark should be cleared
        """
        modifier_keys = {
            "TAB", "LEFT_CONTROL", "RIGHT_CONTROL", "LEFT_ALT", "RIGHT_ALT",
            "LEFT_SHIFT", "RIGHT_SHIFT", "LEFT_SUPER", "RIGHT_SUPER"
        }
        return (
                self.text_marked
                and key_event.type == EventType.PRESS
                and key_event.key not in modifier_keys
        )

    def _delete_word(self, use_whitespace: bool) -> None:
        """
        Delete the word before the cursor.

        Args:
            use_whitespace: If True, use whitespace as delimiter; else use non-alphanumeric
        """
        before, _ = self.line_edit.split_at_cursor()

        if use_whitespace:
            end_pattern = SPACE_PATTERN_END
            delimiter = " "
        else:
            end_pattern = NON_ALPHANUM_PATTERN_END
            delimiter = None

        try:
            start, _ = reindex(before, end_pattern, right=True)
        except ValueError:
            start = -1

        if delimiter:
            try:
                space = before[:start].rindex(delimiter)
            except ValueError:
                space = 0
            self.line_edit.backspace(len(before) - space)
        else:
            # For alphanumeric mode, handle multiple deletion strategies
            if start != -1:
                self.line_edit.backspace(len(before) - start)
            else:
                try:
                    start, _ = reindex(before, NON_ALPHANUM_PATTERN, right=True)
                    self.line_edit.backspace(len(before) - (start + 1))
                except ValueError:
                    self.line_edit.backspace(len(before))

        self.refresh()

    def _move_word_left(self, use_whitespace: bool) -> None:
        """
        Move cursor left by one word/token.

        Args:
            use_whitespace: If True, use whitespace as delimiter; else use non-alphanumeric
        """
        before, _ = self.line_edit.split_at_cursor()

        if use_whitespace:
            end_pattern = SPACE_PATTERN_END
            delimiter = " "
        else:
            end_pattern = NON_ALPHANUM_PATTERN_END
            delimiter = None

        try:
            start, _ = reindex(before, end_pattern, right=True)
        except ValueError:
            start = -1

        if delimiter:
            try:
                space = before[:start].rindex(delimiter)
            except ValueError:
                space = 0
            self.line_edit.left(len(before) - space)
        else:
            if start != -1:
                self.line_edit.left(len(before) - start)
            else:
                try:
                    start, _ = reindex(before, NON_ALPHANUM_PATTERN, right=True)
                    self.line_edit.left(len(before) - (start + 1))
                except ValueError:
                    self.line_edit.left(len(before))

        self.refresh()

    def _move_word_right(self, use_whitespace: bool) -> None:
        """
        Move cursor right by one word/token.

        Args:
            use_whitespace: If True, use whitespace as delimiter; else use non-alphanumeric
        """
        _, after = self.line_edit.split_at_cursor()

        if use_whitespace:
            start_pattern = SPACE_PATTERN_START
            delimiter = " "
        else:
            start_pattern = NON_ALPHANUM_PATTERN_START
            delimiter = None

        try:
            _, end = reindex(after, start_pattern)
        except ValueError:
            end = 0

        if delimiter:
            try:
                space = after[end:].index(delimiter) + 1
            except ValueError:
                space = len(after)
            self.line_edit.right(space)
        else:
            if end > 0:
                self.line_edit.right(end)
            else:
                try:
                    _, end = reindex(after, NON_ALPHANUM_PATTERN)
                    self.line_edit.right(end - 1)
                except ValueError:
                    self.line_edit.right(len(after))

        self.refresh()

    def _navigate_to_match(self, forward: bool) -> None:
        """
        Navigate to the next or previous search match.

        Args:
            forward: If True, go to next match; else go to previous
        """
        args = ["next"] if forward else []
        for match_arg in self.match_args():
            call_remote_control(["kitten", match_arg, str(SCROLLMARK_FILE), *args])

    def on_interrupt(self) -> None:
        """Handle Ctrl+C interrupt."""
        self.quit(1)

    def on_eot(self) -> None:
        """Handle End-of-Transmission (Ctrl+D)."""
        self.quit(1)

    def on_resize(self, screen_size: ScreenSize) -> None:
        """Handle terminal resize events."""
        self.refresh()

    def match_args(self) -> list[str]:
        """
        Generate kitty remote control match arguments for all target windows.

        Returns:
            List of --match=id:N arguments
        """
        return [f"--match=id:{window_id}" for window_id in self.window_ids]

    def mark(self) -> None:
        """
        Create search markers in the target windows.

        Uses debouncing to avoid excessive subprocess calls during typing.
        Markers highlight all matches of the current search term.
        """
        if not self.window_ids:
            return

        text = self.line_edit.current_input
        current_time = time.time()

        # Debounce: skip if text unchanged and marked recently
        if (text == self._last_mark_text and
                current_time - self._last_mark_time < self.MARK_DEBOUNCE_INTERVAL):
            return

        self._last_mark_text = text
        self._last_mark_time = current_time

        if text:
            # Determine case sensitivity: case-insensitive if all lowercase
            match_case = "i" if text.islower() else ""
            match_type = match_case + self.mode

            for match_arg in self.match_args():
                try:
                    call_remote_control(
                        ["create-marker", match_arg, match_type, "1", text]
                    )
                except SystemExit:
                    self.remove_mark()
        else:
            self.remove_mark()

    def remove_mark(self) -> None:
        """Remove all search markers from target windows."""
        for match_arg in self.match_args():
            call_remote_control(["remove-marker", match_arg])

    def quit(self, return_code: int) -> None:
        """
        Clean up and exit the search interface.

        Args:
            return_code: Exit code (0 for success, 1 for cancel)
        """
        # Persist the current search term for next invocation
        self.cached_values["last_search"] = self.line_edit.current_input

        # Clean up markers
        self.remove_mark()

        # On cancel, scroll windows back to their original position
        if return_code:
            for match_arg in self.match_args():
                call_remote_control(["scroll-window", match_arg, "end"])

        self.quit_loop(return_code)


def main(args: list[str]) -> None:
    """
    Entry point for the search kitten.

    Args:
        args: Command line arguments
            args[1]: Window ID (required)
            args[2]: "--all-windows" flag to search all windows in current tab (optional)
    """
    # Resize the search window to minimal vertical space
    call_remote_control(
        ["resize-window", "--self", "--axis=vertical", "--increment", "-100"]
    )

    error = ""
    if len(args) < 2 or not args[1].isdigit():
        error = "Error: Window id must be provided as the first argument."
        window_ids = []
    else:
        window_id = int(args[1])
        window_ids = [window_id]

        # If --all-windows flag is set, search all windows in the current tab
        if len(args) > 2 and args[2] == "--all-windows":
            ls_output = run(["kitty", "@", "ls"], stdout=PIPE)
            ls_json = json.loads(ls_output.stdout.decode())

            # Find the tab containing the current window
            current_tab = None
            for os_window in ls_json:
                for tab in os_window["tabs"]:
                    for kitty_window in tab["windows"]:
                        if kitty_window["id"] == window_id:
                            current_tab = tab
                            break
                    if current_tab:
                        break
                if current_tab:
                    break

            if current_tab:
                # Search all non-focused windows in the current tab
                window_ids = [
                    w["id"] for w in current_tab["windows"] if not w["is_focused"]
                ]
            else:
                error = "Error: Could not find the window id provided."
                window_ids = []

    # Run the search UI loop
    loop = Loop()
    with cached_values_for("search") as cached_values:
        handler = Search(cached_values, window_ids, error)
        loop.loop(handler)
