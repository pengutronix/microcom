import socket
import threading
import time
import pytest
import subprocess
import os
import pty

IAC = bytes([255])
IAC_IAC = bytes([255, 255])
IAC_SB = bytes([255, 250])
IAC_SE = bytes([255, 240])
IAC_WILL_COM_PORT_OPT = bytes([255, 251, 44])
IAC_DO_COM_PORT_OPT = bytes([255, 253, 44])
IAC_DONT_COM_PORT_OPT = bytes([255, 254, 44])


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
                time.sleep(
                    0.01
                )  # sadly without this, microcom may miss the transmission
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
        assert proc.returncode in (0, 1), (
            f"Exit code must be 0 or 1, got {proc.returncode}"
        )

        return bytes(output)

    return _recv


@pytest.mark.parametrize("length", [10, 1023, 1024, 1025, 4000])
def test_no_cmd(telnet_recv, length):
    payload = make_pattern(length)

    assert telnet_recv(payload) == payload


def test_cmd_across_buffers(telnet_recv):
    before_pattern = make_pattern(1023)
    after_pattern = make_pattern(20)
    payload = before_pattern + IAC_DONT_COM_PORT_OPT + after_pattern
    expected_output = before_pattern + after_pattern

    assert telnet_recv(payload) == expected_output


def test_iac_across_buffers(telnet_recv):
    before_pattern = make_pattern(1023)
    after_pattern = make_pattern(20)
    payload = before_pattern + IAC_IAC + after_pattern
    expected_output = before_pattern + IAC + after_pattern

    assert telnet_recv(payload) == expected_output


def test_cmd_buffer_end(telnet_recv):
    pattern = make_pattern(1023)
    payload = pattern + IAC_DONT_COM_PORT_OPT

    assert telnet_recv(payload) == pattern


def test_cmd_buffer_start(telnet_recv):
    before_pattern = make_pattern(1024)
    after_pattern = make_pattern(20)
    payload = before_pattern + IAC_DONT_COM_PORT_OPT + after_pattern

    assert telnet_recv(payload) == before_pattern + after_pattern


def test_cmd_within_buffer(telnet_recv):
    before_pattern = make_pattern(345)
    after_pattern = make_pattern(890)
    payload = before_pattern + IAC_DONT_COM_PORT_OPT + after_pattern
    expected_output = before_pattern + after_pattern

    assert telnet_recv(payload) == expected_output


def test_connect_negotiation(telnet_recv):
    pattern = make_pattern(100)
    payload = IAC_WILL_COM_PORT_OPT + IAC_DO_COM_PORT_OPT + pattern

    assert telnet_recv(payload) == pattern


def test_iac_escape(telnet_recv):
    before_pattern = make_pattern(42)
    after_pattern = make_pattern(42)
    payload = before_pattern + IAC_IAC + after_pattern
    expected_output = before_pattern + IAC + after_pattern

    assert telnet_recv(payload) == expected_output


def test_escape_then_cmd(telnet_recv):
    before_pattern = make_pattern(10)
    after_pattern = make_pattern(10)
    payload = before_pattern + IAC_IAC + IAC_DONT_COM_PORT_OPT + after_pattern
    expected = before_pattern + IAC + after_pattern

    assert telnet_recv(payload) == expected


def test_chained_cmds_split_across_buffers(telnet_recv):
    before = make_pattern(1022)
    after = make_pattern(20)
    payload = before + IAC_DONT_COM_PORT_OPT + IAC_DONT_COM_PORT_OPT + after
    expected = before + after

    assert telnet_recv(payload) == expected


def test_subneg(telnet_recv):
    before_pattern = make_pattern(1022)
    after_pattern = make_pattern(20)
    payload = before_pattern + IAC_SB + make_pattern(10) + IAC_SE + after_pattern

    assert telnet_recv(payload) == before_pattern + after_pattern


def test_subneg_escaped(telnet_recv):
    before = b"before"
    after = b"after"
    # This is not supposed to terminate the subneg: IAC, IAC, SE, !
    payload = before + IAC_SB + bytes([255, 255, 240, 33]) + IAC_SE + after

    assert telnet_recv(payload) == before + after


def test_only_commands(telnet_recv):
    after_pattern = make_pattern(20)
    payload = IAC_SB + make_pattern(1022) + IAC_SE + after_pattern

    assert telnet_recv(payload) == after_pattern
