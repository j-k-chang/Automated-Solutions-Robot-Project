"""
Configuration for the KiCAD pipeline.

Uses pydantic-settings for environment-based configuration.
"""
from pathlib import Path
from typing import Optional

from pydantic import BaseModel, Field


class KiCadConfig(BaseModel):
    """KiCAD installation configuration."""
    kicad_cli_path: Optional[str] = Field(default=None, description="Path to kicad-cli executable")
    kicad_python_path: Optional[str] = Field(default=None, description="Path to KiCAD's bundled Python")
    project_base_dir: Path = Field(default_factory=lambda: Path.home() / ".kicad-pipeline-projects")


class SourcingConfig(BaseModel):
    """Supplier API configuration."""
    digikey_client_id: Optional[str] = None
    digikey_client_secret: Optional[str] = None
    mouser_api_key: Optional[str] = None


class BoardRules(BaseModel):
    """Default design rule configuration."""
    layer_count: int = 4
    trace_width_power: float = 2.0
    trace_width_signal: float = 0.25
    trace_width_ground: float = 0.5
    clearance_min: float = 0.2
    via_diameter: float = 0.6
    via_hole: float = 0.3
    board_thickness: float = 1.6


class Settings(BaseModel):
    """Root settings, composes all sub-configs."""
    kicad: KiCadConfig = KiCadConfig()
    sourcing: SourcingConfig = SourcingConfig()
    board_rules: BoardRules = BoardRules()


settings = Settings()
