"""
Mouser API client for component sourcing.

Full implementation in Phase 4.
"""
from typing import Optional, List, Dict, Any


class MouserClient:
    """Mouser REST API client with API key authentication."""

    def __init__(self, api_key: Optional[str] = None):
        self.api_key = api_key

    def search(self, keyword: str, page: int = 1) -> List[Dict[str, Any]]:
        """Search for parts by keyword."""
        # TODO: Implement API calls (Phase 4)
        raise NotImplementedError("Mouser integration not yet implemented")

    def part_details(self, part_numbers: List[str]) -> List[Dict[str, Any]]:
        """Batch lookup part details for up to 500 MPNs."""
        raise NotImplementedError("Mouser PartDetails not yet implemented")
