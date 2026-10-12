#!/usr/bin/env python3
"""Check that mesen-gdb opens its game window and keeps running: no ROM needed.

    tools/check-dap-window.py bin/osx-arm64/Release/mesen-gdb

Starts a DAP session without --headless, which opens the window with no ROM
loaded, then checks it still answers after a few seconds and exits cleanly on
disconnect. A crash in the window code (e.g. SDL used off the main thread on
macOS) fails it; so does SDL failing to start, which would otherwise fall back
to headless and pass for the wrong reason. Exit status 0 = pass.
"""
import json
import subprocess
import sys
import threading
import time


def main(binary):
    p = subprocess.Popen([binary, '--dap'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    watchdog = threading.Timer(60, p.kill)
    watchdog.daemon = True
    watchdog.start()
    stderr = []
    threading.Thread(target=lambda: stderr.extend(p.stderr.read().decode(errors='replace').splitlines()),
                     daemon=True).start()
    seq = 0

    def request(command, **arguments):
        nonlocal seq
        seq += 1
        body = json.dumps({'seq': seq, 'type': 'request', 'command': command, 'arguments': arguments}).encode()
        p.stdin.write(b'Content-Length: %d\r\n\r\n' % len(body) + body)
        p.stdin.flush()
        while True:
            header = p.stdout.readline()
            if not header:
                raise RuntimeError(f'no reply to {command}: exit {p.wait()}')
            length = int(header.split(b':')[1])
            p.stdout.readline()
            message = json.loads(p.stdout.read(length))
            if message.get('command') == command:
                return message

    failures = []
    if not request('initialize', adapterID='check')['success']:
        failures.append('initialize failed')
    time.sleep(5)
    if p.poll() is not None:
        failures.append(f'exited during the session: {p.returncode}')
    elif not request('threads')['success']:
        failures.append('threads failed after 5 s')
    if p.poll() is None:
        request('disconnect')
        p.stdin.close()
        code = p.wait(30)
        if code != 0:
            failures.append(f'exit {code} after disconnect')
    time.sleep(0.2)
    if any('SDL_Init failed' in line for line in stderr):
        failures.append('SDL did not start, so no window was tested')

    print('\n'.join(stderr[-20:]))
    if failures:
        print('FAIL: ' + '; '.join(failures))
        return 1
    print('PASS: window opened, session answered after 5 s, clean exit')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
