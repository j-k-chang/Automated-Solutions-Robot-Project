"""
KiCAD MCP Server - Data Models
===============================

Pydantic models for type-safe data contracts.
These define the schema that AI agents and pipeline steps interact with.
"""
from typing import List, Optional, Dict, Any, Literal
from datetime import datetime
from pydantic import BaseModel, Field


class KiCadSymbol(BaseModel):
    """A schematic symbol with pins."""
    name: str = Field(..., description="Symbol name (e.g., 'Arduino_Uno_R4')")
    value: str = Field(..., description="Symbol value (e.g., 'Arduino Uno R4 WiFi')")
    reference: str = Field(default="U", description="Reference prefix")
    pins: List[Dict[str, Any]] = Field(default_factory=list, description="List of pin dicts: {number, name, type, unit}")


class KiCadFootprint(BaseModel):
    """A PCB footprint definition."""
    name: str = Field(..., description="Footprint name (e.g., 'QFN-40-0.5mm')")
    package: str = Field(..., description="KiCAD package ID (e.g., 'Package_QFP:QFN-40_6x6mm_P0.5mm')")
    dimensions: Dict[str, float] = Field(default_factory=dict, description="Key dimensions: length, width, pitch")


class ComponentInstance(BaseModel):
    """A placed component on the schematic or PCB."""
    reference: str = Field(..., description="Reference designator (e.g., 'U1')")
    symbol_name: str = Field(..., description="Symbol name from library")
    footprint_name: str = Field(..., description="Footprint name")
    value: str = Field(..., description="Component value")
    position: Optional[Dict[str, float]] = Field(default=None, description="Position on sheet/PCB: {x, y}")
    # Optional sourcing enrichment (populated by step 2)
    mpn: Optional[str] = Field(default=None, description="Manufacturer Part Number")
    manufacturer: Optional[str] = Field(default=None, description="Manufacturer name")
    cost_per_unit: Optional[float] = Field(default=None, description="Cost per unit from supplier")
    quantity_available: Optional[int] = Field(default=None, description="Available quantity from supplier")


class NetConnection(BaseModel):
    """A net connection between two pins."""
    net_name: str = Field(..., description="Net name (e.g., 'UART_TX')")
    from_ref: str = Field(..., description="Source: 'U1:1' (reference:pin)")
    to_ref: str = Field(..., description="Destination: 'U2:RX' (reference:pin)")


class KiCadProjectSpec(BaseModel):
    """Complete specification for a KiCAD project."""
    project_name: str = Field(..., description="Project name (e.g., 'dispenser-pcb')")
    title: str = Field(..., description="Project title")
    author: str = Field(default="AI Agent", description="Author name")
    date: str = Field(default_factory=lambda: datetime.now().strftime("%Y-%m-%d"), description="Date")
    board_dimensions: Dict[str, float] = Field(default_factory=dict, description="Board outline: {width_mm, height_mm}")
    symbols: List[KiCadSymbol] = Field(default_factory=list, description="Schematic symbols to create")
    footprints: List[KiCadFootprint] = Field(default_factory=list, description="PCB footprints to create")
    components: List[ComponentInstance] = Field(default_factory=list, description="Placed components")
    nets: List[NetConnection] = Field(default_factory=list, description="Net connections")


class StepResult(BaseModel):
    """Result of a single pipeline step."""
    step: str
    success: bool
    error: Optional[str] = None
    artifacts: Dict[str, str] = Field(default_factory=dict, description="Output file paths")


class PipelineState(BaseModel):
    """State of the full pipeline execution."""
    project_name: str
    spec: KiCadProjectSpec
    project_dir: str
    step_results: Dict[str, StepResult] = Field(default_factory=dict)
    current_step: Optional[str] = None


class MatchedPart(BaseModel):
    """A part matched from a supplier API."""
    mpn: str
    manufacturer: str
    description: str
    unit_price: float
    quantity_available: int
    supplier: Literal["digikey", "mouser"]
    footprint: Optional[str] = None
    datasheet_url: Optional[str] = None
    lead_time: Optional[str] = None
