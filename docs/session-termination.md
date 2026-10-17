# Session termination

Normal footer/tray quit and window close are interactive. A failed save refuses
exit and keeps the error visible. Explicit discard clears only the selected
native buffer after the other notes have committed. There is no `aboutToQuit`
save, which would revive a deliberately discarded buffer.

System termination is different: a session already ending must not wait for a
save/discard prompt. `SessionExit` owns this policy in the shell. The document
engine's `flushForSessionEnd()` attempts every dirty native document once and
retries its recovery journal when the Markdown commit fails. A conflicted file
is never overwritten. Failure is reported without vetoing termination.

## Entry points

- Qt `commitDataRequest` and `saveStateRequest` synchronously flush native
  buffers. They do not quit: the request may be a session checkpoint.
- Plasma Wayland sends ordinary `xdg_toplevel.close` events through KWin's
  `closeWaylandWindows()`. Qt Wayland does not provide an XSMP callback for
  this. Both the dock and pinned windows ask the existing KSMServer
  `isShuttingDown` method on close, using a 250 ms D-Bus timeout. Only a valid
  true reply chooses non-interactive termination; false, absent or failed
  replies retain normal close behavior. Footer and tray actions do not query it.
  KSMServer also reports true during Checkpoint, so a window close then exits instead of prompting.
- SIGTERM writes one byte to a nonblocking, close-on-exec pipe. A socket notifier
  performs the flush and exits on the GUI thread. The signal handler itself
  calls no Qt or storage APIs. Repeated signals coalesce; a full pipe already
  means termination is pending.

`QCoreApplication::exit()` ends the loop after the forced flush, without another
vetoable close. Session-end closure does not unpin a note.

## Verification and limits

`fanfold-shutdown` runs the real application bootstrap, engine and QML on Xvfb
with an isolated HOME, runtime directory and D-Bus session. A private service
implements only the KDE shutdown-state method. The probe delivers real Qt window
closes, Qt session signals and SIGTERM. It checks Markdown bytes, recovery JSON,
pin retention, failed-save refusal for ordinary close, and termination deadlines.
It does not log out or shut down the host, or claim a full Plasma session test.

The contract tested here is native pending Markdown, already delivered through
the editor bridge. Text still in Chromium or in an unacknowledged WebChannel
call is not covered by these forced synchronous flushes. An unresponsive storage
syscall also cannot be bounded by a GUI timer. If both Markdown and recovery
storage fail, the error is logged and termination still proceeds; durability
cannot be guaranteed in that condition. SIGKILL and power loss are not graceful
termination paths.

Primary protocol references:

- [Plasma shutdown orchestration](https://github.com/KDE/plasma-workspace/blob/v6.6.6/startkde/plasma-shutdown/shutdown.cpp)
- [KSMServer state](https://github.com/KDE/plasma-workspace/blob/v6.6.6/ksmserver/logout.cpp)
- [KWin Wayland window closure](https://github.com/KDE/kwin/blob/v6.6.6/src/sm.cpp)
