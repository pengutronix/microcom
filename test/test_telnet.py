import socket
import threading
import time
import pytest
import subprocess
import os
import pty

RFC2217_CMD = bytes([255, 254, 44])  # RFC2217 IAC DONT COM-PORT-OPTION


def make_pattern(length):
    ascii_range = list(range(32, 127))
    return bytes(ascii_range[i % len(ascii_range)] for i in range(length))


@pytest.fixture
def telnet_recv(cmd):
    def _recv(buf, timeout=1):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(("127.0.0.1", 0))
        sock.listen(1)
        host, port = sock.getsockname()

        def run():
            conn, _ = sock.accept()
            with conn:
                conn.sendall(buf)
                time.sleep(0.01)  # sadly without this, microcom may miss the transmission
                sock.close()

        thread = threading.Thread(target=run, daemon=True)
        thread.start()

        telnet_cmd = cmd + [f"--telnet={host}:{port}", "--quiet"]
        master_fd, slave_fd = pty.openpty()
        proc = subprocess.Popen(
            telnet_cmd,
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=os.dup(2),
            close_fds=True,
        )
        os.close(slave_fd)
        output = bytearray()
        start_time = time.time()
        while (time.time() - start_time) < timeout:
            try:
                chunk = os.read(master_fd, 1024)
                if not chunk:
                    break
                output.extend(chunk)
            except OSError:
                break
        os.close(master_fd)
        proc.wait()
        assert proc.returncode in (0, 1)

        return bytes(output)
    return _recv


@pytest.mark.parametrize("buf", [10, 1023, 1024, 1025, 4000])
def test_no_cmd(telnet_recv, buf):
    payload = make_pattern(buf)

    assert telnet_recv(payload) == payload


def test_cmd_across_buffers(telnet_recv):
    before_pattern = make_pattern(1023)
    after_pattern = make_pattern(20)
    payload = before_pattern + RFC2217_CMD + after_pattern
    expected_output = before_pattern + after_pattern

    assert telnet_recv(payload) == expected_output


def test_cmd_buffer_end(telnet_recv):
    pattern = make_pattern(1023)
    payload = pattern + RFC2217_CMD

    assert telnet_recv(payload) == pattern


def test_cmd_within_buffer(telnet_recv):
    before_pattern = make_pattern(345)
    after_pattern = make_pattern(890)
    payload = before_pattern + RFC2217_CMD + after_pattern
    expected_output = before_pattern + after_pattern

    assert telnet_recv(payload) == expected_output


def test_iac_escape(telnet_recv):
    before_pattern = make_pattern(42)
    after_pattern = make_pattern(42)
    payload = before_pattern + bytes([255, 255]) + after_pattern
    expected_output = before_pattern + bytes([255]) + after_pattern

    assert telnet_recv(payload) == expected_output
