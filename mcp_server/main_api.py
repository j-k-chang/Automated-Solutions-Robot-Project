"""
File: mcp_server/main_api.py
Reasoning: This is the main entry point for the MCP Server.
It has been migrated to the official Anthropic Model Context Protocol (MCP).
"""
import os
import sys
import time
import json
import asyncio

# Add parent directory to path so absolute imports work
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

from mcp.server.fastmcp import FastMCP, Context  # noqa: E402
from mcp_server.models import SystemRegistry, SystemState  # noqa: E402

# Initialize FastMCP Server
mcp = FastMCP("LiquidDispenser")

# --- Global State & Initialization ---
try:
    BASE_DIR = os.path.dirname(os.path.abspath(__file__))
    RECIPE_FILE = os.path.join(BASE_DIR, "recipes.json")
    with open(RECIPE_FILE, 'r') as f:
        recipe_data = json.load(f)
    print("Successfully loaded recipe data structure.", file=os.sys.stderr)
except Exception as e:
    print(f"WARNING: Failed to load recipes: {e}", file=os.sys.stderr)
    recipe_data = {}

system_registry = SystemRegistry(systems={})


def load_initial_state():
    """Loads initial known system states (Mock function)."""
    system_registry.systems['pump_A'] = SystemState(
        system_id='pump_A',
        current_weight_g=1000.0,
        operational_state='IDLE',
        last_update_timestamp=time.time()
    )
    system_registry.systems['pump_B'] = SystemState(
        system_id='pump_B',
        current_weight_g=500.0,
        operational_state='IDLE',
        last_update_timestamp=time.time()
    )


load_initial_state()


@mcp.tool()
def get_system_status() -> str:
    """Returns the current aggregated state of all connected pumps."""
    return json.dumps(system_registry.model_dump())


@mcp.tool()
async def dispense_direct(
    pump_id: str, chemical_code: str, target_volume_ml: float, ctx: Context
) -> str:
    """Executes a direct dispense command for a specific pump and chemical."""
    if pump_id not in system_registry.systems:
        return f"Error: Pump '{pump_id}' not found in connected systems."

    ctx.info(f"Starting {target_volume_ml}ml {chemical_code} from {pump_id}.")
    system_registry.systems[pump_id].operational_state = 'POURING'

    # Simulate the physical pumping action over time (Live Feed)
    steps = 5
    vol_per_step = target_volume_ml / steps
    for i in range(steps):
        await asyncio.sleep(1)  # Simulate 1s physical hardware time per step
        system_registry.systems[pump_id].current_weight_g -= vol_per_step
        system_registry.systems[pump_id].last_update_timestamp = time.time()
        ctx.info(
            f"[{pump_id}] Progress: "
            f"Dispensed {(i+1)*vol_per_step:.1f}ml of {target_volume_ml:.1f}ml"
        )

    system_registry.systems[pump_id].operational_state = 'IDLE'
    return (
        f"Successfully dispensed {target_volume_ml}ml of "
        f"{chemical_code} from {pump_id}."
    )


@mcp.tool()
async def dispense_recipe(recipe_name: str, ctx: Context) -> str:
    """Executes a physical dispense sequence based on a known recipe name."""
    recipes = recipe_data.get("Recipes", [])
    target_recipe = next(
        (r for r in recipes if r.get("recipe_name") == recipe_name), None
    )

    if not target_recipe:
        available = [r.get("recipe_name") for r in recipes]
        return f"Error: Recipe '{recipe_name}' not found. Available: {available}"

    ctx.info(f"Initiating dispensing sequence for recipe: {recipe_name}")

    for step in target_recipe.get("steps", []):
        pump_id = step.get("pump_id")
        chemical = step.get("chemical_code")
        vol = step.get("target_volume_ml")

        ctx.info(f"--- Step: {chemical} ({vol}ml) from {pump_id} ---")
        # Call the direct dispense logic to execute this step
        step_result = await dispense_direct(pump_id, chemical, vol, ctx)
        ctx.info(f"Step Complete: {step_result}")

    return f"Recipe '{recipe_name}' completed successfully."


if __name__ == '__main__':
    print("Starting MCP Liquid Dispenser Server...", file=os.sys.stderr)
    mcp.run()
