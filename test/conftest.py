import os
from pathlib import Path
import shlex
import pytest


def pytest_sessionstart(session):
    # run tests in toplevel directory always, not in test/
    os.chdir(Path(__file__).resolve().parent.parent)


def pytest_addoption(parser):
    parser.addoption(
        "--cmd",
        action="store",
        default="./microcom",
        help="Command used to invoke microcom"
    )


@pytest.fixture(scope="session")
def cmd(pytestconfig):
    cmd = pytestconfig.getoption("--cmd")
    return shlex.split(cmd)
