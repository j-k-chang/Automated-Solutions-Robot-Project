"""
File: mcp_server/models.py
Reasoning: This file centralizes all data models using Pydantic for type safety and validation. It defines the structure for Recipes, Chemical definitions, and the expected state of any connected dispensing system (System State). Using classes ensures that incoming data from the AI or recipes adhere to a strict contract.
"""
from typing import List, Optional, Dict
from pydantic import BaseModel, Field

class ChemicalDefinition(BaseModel):
    """Defines a chemical compound used in the process."""
    chemical_code: str = Field(..., description="Unique identifier for the chemical (e.g., 'HCl', 'NaOH').")
    name: str = Field(..., description="Human-readable name.")
    description: Optional[str] = Field(None, description="Details about the chemical safety or use.")

class DispenseStep(BaseModel):
    """Defines a single action step within a recipe."""
    pump_id: str = Field(..., description="The unique System ID of the Arduino/Pump responsible for this step (e.g., 'pump_A').")
    chemical_code: str = Field(..., description="The chemical to dispense, matching a ChemicalDefinition.")
    target_volume_ml: float = Field(..., ge=0.1, description="Target volume in milliliters (must be > 0).")
    dispense_duration_s: Optional[float] = Field(None, ge=0, description="Optional override for dispense time if needed.")

class Recipe(BaseModel):
    """Defines an entire dispensing sequence."""
    recipe_name: str = Field(..., description="User-friendly name for the recipe (e.g., 'pH Neutralization').")
    steps: List[DispenseStep] = Field(..., description="The ordered list of steps required to execute this recipe.")

class SystemState(BaseModel):
    """Represents the current, aggregated status of a single connected dispensing unit."""
    system_id: str = Field(..., description="Unique ID identifying this pump/Arduino.")
    current_weight_g: float = Field(0.0, description="Last known measured weight from the scale in grams.")
    operational_state: str = Field(..., description="Current state of the dispensing process (e.g., 'IDLE', 'POURING', 'SETTLING', 'ERROR').")
    last_update_timestamp: float # Removed default to force setting upon creation/update

class SystemRegistry(BaseModel):
    """A container model to hold states for all known, connected systems."""
    systems: Dict[str, SystemState] = Field(default_factory=dict)
