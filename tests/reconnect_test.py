#!/usr/bin/env python3
"""Integration test: outbound reconnect policy of the sendspin-client binary.

Drives the real client binary against a minimal in-process SendSpin server
stub (raw WebSocket, no dependencies) and verifies the policy that moved
client-side with sendspin-cpp v0.8.0:

1. An established outbound connection reconnects after the transport dies.
2. A blackholed connection is dropped by the liveness watchdog and reconnects.
3. Retry gaps grow while the server is unreachable (1 s doubling backoff).
4. Backoff resets after a successful reconnect (next recovery is quick).
5. RECONNECT_ON_LOSS=false leaves the connection dropped.

Registered with CTest when BUILD_TESTING=ON. See CMakeLists.txt.
"""

import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

RECONNECT_LINE = ">>> Connection lost, reconnecting to"
HANDSHAKE_LINE = "Connection handshake complete: server_id=fake-server"
WATCHDOG_LINE = "Current connection silent for >"

LIVENESS_S = 3


class FakeServer:
    """Minimal SendSpin server: answers client/hello and client/time.

    One listener plus one connection handler thread per accepted socket.
    kill_connections() drops live sockets abruptly (SO_LINGER 0 -> RST),
    mute=True stops answering so the client's inbound silence accrues and
    its liveness watchdog fires, and the listener can be closed and
    re-opened on the same port to make reconnect attempts fail.
    """

    def __init__(self, port):
        self.port = port
        self.hello_count = 0
        self.muted = False
        self.lock = threading.Lock()
        self.live = []
        self.listener = None
        self.accept_thread = None
        self.stopped = threading.Event()
        self.start_listener()

    def start_listener(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", self.port))
        srv.listen(4)
        srv.settimeout(0.2)
        self.listener = srv
        self.accept_thread = threading.Thread(target=self._accept_loop, daemon=True)
        self.accept_thread.start()

    def stop_listener(self):
        if self.listener is not None:
            try:
                self.listener.close()
            except OSError:
                pass
            self.listener = None

    def _accept_loop(self):
        while not self.stopped.is_set():
            if self.listener is None:
                time.sleep(0.05)
                continue
            try:
                conn, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _serve(self, sock):
        try:
            self._handshake(sock)
            with self.lock:
                self.live.append(sock)
            while True:
                opcode, payload = self._read_frame(sock)
                if opcode == 8:  # close
                    self._send_frame(sock, 8, b"")
                    return
                if opcode == 9:  # ping -> pong
                    self._send_frame(sock, 10, payload)
                    continue
                if opcode != 1:
                    continue
                text = payload.decode(errors="replace")
                if "client/hello" in text:
                    with self.lock:
                        self.hello_count += 1
                        muted = self.muted
                    if not muted:
                        self._send_text(sock, self._server_hello())
                elif "client/time" in text:
                    with self.lock:
                        muted = self.muted
                    if not muted:
                        self._answer_time(sock, text)
        except (ConnectionError, OSError, ValueError):
            pass
        finally:
            try:
                sock.close()
            except OSError:
                pass

    @staticmethod
    def _server_hello():
        return json.dumps(
            {
                "type": "server/hello",
                "payload": {
                    "server_id": "fake-server",
                    "name": "Fake Server",
                    "version": 1,
                    "active_roles": ["player"],
                    "connection_reason": "discovery",
                },
            }
        )

    def _handshake(self, sock):
        request = b""
        while b"\r\n\r\n" not in request:
            chunk = sock.recv(4096)
            if not chunk:
                raise ConnectionError("eof during upgrade")
            request += chunk
        key = b""
        for line in request.split(b"\r\n"):
            if line.lower().startswith(b"sec-websocket-key:"):
                key = line.split(b":", 1)[1].strip()
        if not key:
            raise ValueError("no Sec-WebSocket-Key")
        accept = base64.b64encode(hashlib.sha1(key + WS_GUID.encode()).digest()).decode()
        sock.sendall(
            (
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                f"Sec-WebSocket-Accept: {accept}\r\n\r\n"
            ).encode()
        )

    @staticmethod
    def _read_frame(sock):
        hdr = FakeServer._recv_exact(sock, 2)
        opcode = hdr[0] & 0x0F
        masked = hdr[1] & 0x80
        length = hdr[1] & 0x7F
        if length == 126:
            length = struct.unpack(">H", FakeServer._recv_exact(sock, 2))[0]
        elif length == 127:
            length = struct.unpack(">Q", FakeServer._recv_exact(sock, 8))[0]
        mask = FakeServer._recv_exact(sock, 4) if masked else None
        payload = FakeServer._recv_exact(sock, length) if length else b""
        if mask:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        return opcode, payload

    @staticmethod
    def _recv_exact(sock, n):
        data = b""
        while len(data) < n:
            chunk = sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("eof")
            data += chunk
        return data

    @staticmethod
    def _send_frame(sock, opcode, payload):
        header = bytes([0x80 | opcode])
        n = len(payload)
        if n < 126:
            header += bytes([n])
        elif n < 65536:
            header += bytes([126]) + struct.pack(">H", n)
        else:
            header += bytes([127]) + struct.pack(">Q", n)
        sock.sendall(header + payload)

    @classmethod
    def _send_text(cls, sock, text):
        cls._send_frame(sock, 1, text.encode())

    def _answer_time(self, sock, text):
        pos = text.find('"client_transmitted":')
        if pos < 0:
            return
        digits = ""
        for ch in text[pos + 21:]:
            if ch.isdigit():
                digits += ch
            elif digits:
                break
        now = time.time_ns() // 1000
        self._send_text(
            sock,
            json.dumps(
                {
                    "type": "server/time",
                    "payload": {
                        "client_transmitted": int(digits or "0"),
                        "server_received": now,
                        "server_transmitted": now,
                    },
                }
            ),
        )

    def kill_connections(self):
        """Drop every live socket abruptly (RST, no WebSocket close frame)."""
        with self.lock:
            sockets = list(self.live)
            self.live = []
        for sock in sockets:
            try:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            except OSError:
                pass
            try:
                sock.close()
            except OSError:
                pass

    def set_muted(self, muted):
        with self.lock:
            self.muted = muted

    def hello_count_now(self):
        with self.lock:
            return self.hello_count

    def stop(self):
        self.stopped.set()
        self.stop_listener()


class ClientLog:
    """Drains a subprocess stderr pipe, timestamping each line."""

    def __init__(self, pipe):
        self.entries = []
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._drain, args=(pipe,), daemon=True)
        self.thread.start()

    def _drain(self, pipe):
        for raw in pipe:
            with self.lock:
                self.entries.append((time.monotonic(), raw.decode(errors="replace").rstrip()))

    def lines(self):
        with self.lock:
            return list(self.entries)

    def count(self, needle):
        return sum(1 for _, line in self.lines() if needle in line)

    def timestamps(self, needle):
        return [t for t, line in self.lines() if needle in line]

    def wait_for(self, needle, count, deadline_s, step=0.05):
        deadline = time.monotonic() + deadline_s
        while time.monotonic() < deadline:
            if self.count(needle) >= count:
                return True
            time.sleep(step)
        return False


def free_port():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def wait_until(pred, deadline_s, step=0.05, message="condition not met", log=None):
    deadline = time.monotonic() + deadline_s
    while time.monotonic() < deadline:
        if pred():
            return
        time.sleep(step)
    if log is not None:
        print("--- client log at failure ---", file=sys.stderr)
        t0 = log.lines()[0][0] if log.lines() else 0
        for t, line in log.lines():
            print(f"[{t - t0:7.2f}] {line}", file=sys.stderr)
    raise AssertionError(f"timeout: {message}")


def start_client(client_path, conf_path, client_port, stub_port):
    proc = subprocess.Popen(
        [
            client_path,
            "-c", conf_path,
            "-p", str(client_port),
            "-u", f"ws://127.0.0.1:{stub_port}/sendspin",
            "-l", "info",
            "reconnect-test",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    return proc, ClientLog(proc.stderr)


def stop_client(proc):
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)
    assert proc.returncode == 0, f"client did not shut down cleanly (exit {proc.returncode})"


def write_conf(workdir, name, reconnect_enabled):
    conf_path = os.path.join(workdir, name)
    with open(conf_path, "w") as fh:
        fh.write(
            f"LIVENESS_TIMEOUT = {LIVENESS_S}\n"
            f"RECONNECT_ON_LOSS = {'true' if reconnect_enabled else 'false'}\n"
            "ENABLE_MDNS = false\n"
        )
    return conf_path


def test_reconnect_enabled(client_path, workdir):
    """Full cycle: recovery, watchdog drop, backoff growth, reset after success."""
    stub_port = free_port()
    client_port = free_port()
    conf_path = write_conf(workdir, "enabled.conf", True)
    stub = FakeServer(stub_port)
    proc, log = None, None
    try:
        proc, log = start_client(client_path, conf_path, client_port, stub_port)

        # Phase A: initial handshake
        wait_until(lambda: stub.hello_count_now() >= 1, 15,
                   message="initial handshake never completed")
        assert log.wait_for(HANDSHAKE_LINE, 1, 5), "no handshake-complete log line"

        # Phase B: transport loss (RST) -> immediate reconnect -> recovery
        stub.kill_connections()
        assert log.wait_for(RECONNECT_LINE, 1, LIVENESS_S + 10), \
            "no reconnect attempt after transport loss"
        wait_until(lambda: stub.hello_count_now() >= 2, 10,
                   message="no second handshake after transport loss")

        # Phase C: blackhole (mute) with the listener down -> watchdog drop,
        # then failing retries with growing gaps
        stub.set_muted(True)
        stub.stop_listener()
        assert log.wait_for(WATCHDOG_LINE, 1, LIVENESS_S + 10), \
            "liveness watchdog never fired on a muted connection"
        assert log.wait_for(RECONNECT_LINE, 2, LIVENESS_S + 10), \
            "expected a second reconnect attempt after the watchdog drop"
        wait_until(lambda: len(log.timestamps(RECONNECT_LINE)) >= 4, 25,
                   message="expected at least 4 reconnect attempts (backoff phase)")
        gaps = [b - a for a, b in
                zip(log.timestamps(RECONNECT_LINE), log.timestamps(RECONNECT_LINE)[1:])]
        # Nominal gaps once failing: 2 s then 4 s (first retry after the drop is immediate).
        # Margins are generous; only a non-growing backoff should trip these.
        assert gaps[-2] >= 1.2, f"retry gap does not grow (got {gaps[-2]:.2f}s)"
        assert gaps[-1] >= gaps[-2] + 0.8, \
            f"retry gaps do not double (got {gaps[-2]:.2f}s then {gaps[-1]:.2f}s)"

        # Phase D: server returns -> next due retry reconnects
        stub.set_muted(False)
        stub.start_listener()
        wait_until(lambda: stub.hello_count_now() >= 3, 15,
                   message="no handshake after the server came back")

        # Phase E: transport loss again -> recovery must be prompt (backoff was reset by the
        # Phase D success). The stub's client handler sends a close frame on EOF, so the client
        # sees a clean close: is_connected() flips false immediately and the policy reconnects
        # without waiting for the watchdog.
        prev_attempts = log.count(RECONNECT_LINE)
        stub.kill_connections()

        def prompt_retry():
            stamps = log.timestamps(RECONNECT_LINE)
            return len(stamps) >= prev_attempts + 1 and \
                stamps[-1] - t_recovered < 2.0 + LIVENESS_S

        t_recovered = log.timestamps(HANDSHAKE_LINE)[-1]
        wait_until(prompt_retry, 2.0 + LIVENESS_S + 2,
                   message="reconnect after a recovery was not prompt (backoff not reset?)",
                   log=log)
        wait_until(lambda: stub.hello_count_now() >= 4, 10,
                   message="no fourth handshake after backoff reset")
    finally:
        if proc is not None:
            stop_client(proc)
        stub.stop()


def test_reconnect_disabled(client_path, workdir):
    """RECONNECT_ON_LOSS=false: the watchdog drops, nothing reconnects."""
    stub_port = free_port()
    client_port = free_port()
    conf_path = write_conf(workdir, "disabled.conf", False)
    stub = FakeServer(stub_port)
    proc, log = None, None
    try:
        proc, log = start_client(client_path, conf_path, client_port, stub_port)
        wait_until(lambda: stub.hello_count_now() >= 1, 15,
                   message="initial handshake never completed")

        stub.set_muted(True)
        assert log.wait_for(WATCHDOG_LINE, 1, LIVENESS_S + 10), \
            "liveness watchdog never fired on a muted connection"
        time.sleep(4)
        assert log.count(RECONNECT_LINE) == 0, \
            "reconnect attempted despite RECONNECT_ON_LOSS=false"
        assert stub.hello_count_now() == 1, \
            "new handshake despite RECONNECT_ON_LOSS=false"
    finally:
        if proc is not None:
            stop_client(proc)
        stub.stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", required=True, help="path to the sendspin-client binary")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="sendspin-reconnect-test-") as workdir:
        test_reconnect_enabled(args.client, workdir)
        test_reconnect_disabled(args.client, workdir)

    print("reconnect test: all scenarios passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
