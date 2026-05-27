"""
Step 2: Component Selection.

Looks up parts from DigiKey/Mouser APIs and enriches the spec with
MPN, manufacturer, cost, and availability data.

Currently a stub — full implementation in Phase 4.
"""
from typing import Dict, Any

from models import KiCadProjectSpec


def run(spec: KiCadProjectSpec, project_dir: str, **kwargs) -> Dict[str, str]:
    """
    Enrich spec with component sourcing data.

    Args:
        spec: KiCadProjectSpec (will be enriched with MPN/cost data)
        project_dir: Project output directory
        **kwargs: Optional digikey_client_id, digikey_client_secret, mouser_api_key

    Returns:
        Dict of artifact name -> file path
    """
    # TODO: Implement DigiKey/Mouser integration (Phase 4)
    # For now, skip with a note
    return {"status": "skipped", "message": "Component selection not yet implemented. Configure API keys and re-run."}
