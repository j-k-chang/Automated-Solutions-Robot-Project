"""
Subprocess wrapper for kicad-cli commands.

Handles timeout, error capture, and path resolution for all kicad-cli invocations.
"""
import subprocess
from pathlib import Path
from typing import Optional, List
import os


class KiCADNotFoundError(Exception):
    """Raised when kicad-cli is not found."""
    pass


class KiCADCommandError(Exception):
    """Raised when a kicad-cli command fails."""
    def __init__(self, command: List[str], returncode: int, stdout: str, stderr: str):
        self.command = command
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
        super().__init__(f"kicad-cli command failed (rc={returncode}): {' '.join(command)}\nstderr: {stderr[:500]}")


def _resolve_kicad_cli(cli_path: Optional[str] = None) -> str:
    """Resolve the kicad-cli executable path."""
    if cli_path:
        return cli_path
    # Try to find in PATH
    import shutil
    path = shutil.which("kicad-cli")
    if path:
        return path

    # Windows default KiCAD installation paths with version globbing
    import glob
    candidates = []
    
    # Try to find any version installed under C:\Program Files\KiCad
    windows_glob = r"C:\Program Files\KiCad\*\bin\kicad-cli.exe"
    found_paths = glob.glob(windows_glob)
    if found_paths:
        # Sort in reverse order to prefer newer versions (e.g. 10.0 over 8.0)
        found_paths.sort(reverse=True)
        candidates.extend(found_paths)

    candidates.extend([
        r"C:\Program Files\KiCad\KiCad 8.0\bin\kicad-cli.exe",
        r"C:\Program Files\KiCad\bin\kicad-cli.exe",
        "/usr/bin/kicad-cli",
        "/opt/kicad/bin/kicad-cli",
    ])
    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate
    raise KiCADNotFoundError("kicad-cli not found. Install KiCAD 8.0+ or set KICAD_KICAD_CLI_PATH environment variable.")


def run_kicad_cli(
    *args,
    extra_args: Optional[List[str]] = None,
    project_file: Optional[str] = None,
    timeout: int = 120,
    cli_path: Optional[str] = None,
) -> str:
    """
    Run a kicad-cli command and return stdout+stderr combined.

    Args:
        *args: positional arguments (e.g., "pcb", "drc")
        extra_args: additional arguments
        project_file: path to .kicad_pro project file
        timeout: command timeout in seconds
        cli_path: optional path to kicad-cli executable

    Returns:
        Combined stdout + stderr output

    Raises:
        KiCADNotFoundError: if kicad-cli is not found
        KiCADCommandError: if the command fails (non-zero exit code)
    """
    cmd = [_resolve_kicad_cli(cli_path)] + list(args)
    if extra_args:
        cmd.extend(extra_args)
    if project_file:
        # Insert --project before extra positional args that follow
        cmd.append(f"--project={project_file}")

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        if result.returncode != 0:
            raise KiCADCommandError(cmd, result.returncode, result.stdout, result.stderr)
        return result.stdout + result.stderr
    except FileNotFoundError as e:
        raise KiCADNotFoundError(str(e))
    except subprocess.TimeoutExpired:
        raise KiCADCommandError(cmd, -1, "", f"Command timed out after {timeout}s")


def run_kicad_cli_json(
    *args,
    extra_args: Optional[List[str]] = None,
    project_file: Optional[str] = None,
    timeout: int = 120,
    cli_path: Optional[str] = None,
) -> dict:
    """
    Run a kicad-cli command that outputs JSON and parse it.

    Args: same as run_kicad_cli

    Returns:
        Parsed JSON dict

    Raises:
        KiCADNotFoundError, KiCADCommandError
    """
    import json

    output = run_kicad_cli(*args, extra_args=extra_args, project_file=project_file, timeout=timeout, cli_path=cli_path)
    return json.loads(output)


def run_kicad_python(
    script: str,
    project_file: Optional[str] = None,
    timeout: int = 300,
    python_path: Optional[str] = None,
) -> str:
    """
    Run a Python script using KiCAD's bundled Python interpreter.

    Args:
        script: path to Python script or inline code (use file path for complex scripts)
        project_file: path to .kicad_pro project file (sets working directory)
        timeout: command timeout in seconds
        python_path: optional path to KiCAD's Python interpreter

    Returns:
        stdout+stderr output

    Raises:
        KiCADNotFoundError: if KiCAD Python is not found
    """
    if python_path:
        python_exe = python_path
    else:
        import shutil
        python_exe = shutil.which("kicad-cli")
        if python_exe:
            # kicad-cli python subcommand
            python_exe = python_exe
        else:
            for candidate in [
                r"C:\Program Files\KiCad\KiCad 8.0\python.exe",
                r"C:\Program Files\KiCad\python.exe",
                "/usr/bin/python3",
            ]:
                if os.path.exists(candidate):
                    python_exe = candidate
                    break
            else:
                python_exe = "python3"  # fallback to system Python

    cmd = [python_exe, script]
    if project_file:
        cmd.append(project_file)

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
            cwd=os.path.dirname(project_file) if project_file else None,
        )
        if result.returncode != 0:
            raise KiCADCommandError(cmd, result.returncode, result.stdout, result.stderr)
        return result.stdout + result.stderr
    except FileNotFoundError as e:
        raise KiCADNotFoundError(str(e))
