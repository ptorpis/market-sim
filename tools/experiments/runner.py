"""
Single simulation run wrapper.

Writes a config dict to a temporary JSON file, invokes the C++ binary with
--config and --output flags, and returns the output directory on success.
"""

from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path


def run_simulation(
    config: dict,
    output_dir: Path,
    binary: Path = Path("./build/debug/MarketSimulator"),
    *,
    timeout: int = 300,
    db_connection_string: str | None = None,
) -> Path:
    """
    Run one simulation with the given config dict.

    Writes the config dict to a temporary JSON file, invokes the binary with
    --config and --output flags, and returns output_dir on success. The binary's
    output_dir field in the config is overridden by the --output flag so the
    caller controls where results land.

    Args:
        config: Config dict as produced by build_gm_config.
        output_dir: Directory where the simulator writes CSVs and metadata.json.
        binary: Path to the compiled MarketSimulator binary.
        timeout: Maximum seconds to wait before raising subprocess.TimeoutExpired.

    Returns:
        output_dir after a successful run.

    Raises:
        RuntimeError: If the binary exits with a non-zero status code.
        subprocess.TimeoutExpired: If the run exceeds timeout seconds.
    """
    if db_connection_string is not None:
        config = {
            **config,
            "persistence": {
                "backend": "postgres",
                "connection_string": db_connection_string,
                "batch_size": 50000,
            },
        }

    output_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.NamedTemporaryFile(
        mode="w",
        suffix=".json",
        delete=False,
        encoding="utf-8",
    ) as tmp:
        json.dump(config, tmp, indent=2)
        config_path = Path(tmp.name)

    try:
        result = subprocess.run(
            [str(binary), "--config", str(config_path), "--output", str(output_dir)],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    finally:
        config_path.unlink(missing_ok=True)

    if result.returncode != 0:
        raise RuntimeError(
            f"Simulator exited with code {result.returncode}.\n"
            f"stderr: {result.stderr}\nstdout: {result.stdout}"
        )

    return output_dir


def read_run_id(output_dir: Path) -> str | None:
    """Return the run_id UUID written by the simulator, or None if not present."""
    run_id_path = output_dir / "run_id.txt"
    if not run_id_path.exists():
        return None
    return run_id_path.read_text(encoding="utf-8").strip()
