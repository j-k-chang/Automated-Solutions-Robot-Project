"""
Pipeline orchestrator for the KiCAD PCB design pipeline.

Executes steps sequentially with resume support and error tracking.
"""
import json
import os
from pathlib import Path
from typing import Optional, List, Dict, Any
from datetime import datetime

from models import KiCadProjectSpec, StepResult, PipelineState


class PipelineError(Exception):
    """Base error for pipeline failures."""
    def __init__(self, step: str, message: str, recoverable: bool = True):
        super().__init__(message)
        self.step = step
        self.recoverable = recoverable


class KiCADNotFoundError(PipelineError):
    """KiCAD CLI not installed or not found."""
    pass


AVAILABLE_STEPS = [
    "generate_schematic",
    "component_selection",
    "pcb_layout",
    "validation",
    "manufacturing",
]


class PipelineOrchestrator:
    """Orchestrates the 5-step KiCAD design pipeline."""

    def __init__(self, spec: KiCadProjectSpec, project_dir: Optional[str] = None):
        self.spec = spec
        self.project_dir = project_dir or self._create_project_dir(spec.project_name)
        self.state = PipelineState(
            project_name=spec.project_name,
            spec=spec,
            project_dir=self.project_dir,
        )
        self._load_previous_state()

    def _create_project_dir(self, project_name: str) -> str:
        """Create and return the project directory."""
        base = Path.home() / ".kicad-pipeline-projects"
        base.mkdir(parents=True, exist_ok=True)
        project_dir = base / project_name
        project_dir.mkdir(parents=True, exist_ok=True)
        return str(project_dir)

    def _load_previous_state(self):
        """Load previous pipeline state if it exists."""
        state_file = Path(self.project_dir) / "pipeline_state.json"
        if state_file.exists():
            try:
                data = json.loads(state_file.read_text())
                self.state.step_results = {
                    k: StepResult(**v) for k, v in data.get("step_results", {}).items()
                }
                self.state.current_step = data.get("current_step")
            except (json.JSONDecodeError, KeyError):
                pass  # Start fresh on corrupt state

    def _save_state(self):
        """Persist pipeline state to disk."""
        state_file = Path(self.project_dir) / "pipeline_state.json"
        data = {
            "project_name": self.state.project_name,
            "project_dir": self.state.project_dir,
            "current_step": self.state.current_step,
            "step_results": {k: v.model_dump() for k, v in self.state.step_results.items()},
        }
        state_file.write_text(json.dumps(data, indent=2))

    def get_step_status(self) -> Dict[str, bool]:
        """Return success status for each completed step."""
        return {step: result.success for step, result in self.state.step_results.items()}

    def should_run_step(self, step: str) -> bool:
        """Check if a step should be re-run (not already completed successfully)."""
        return step not in self.state.step_results or not self.state.step_results[step].success

    def execute_step(self, step: str, step_func, **kwargs) -> StepResult:
        """
        Execute a single pipeline step.

        Args:
            step: step name identifier
            step_func: callable(spec, project_dir, **kwargs) -> Dict[str, str] (artifacts)
            **kwargs: additional arguments passed to step_func

        Returns:
            StepResult with success status and artifacts
        """
        self.state.current_step = step
        artifacts = {}
        error = None
        success = False

        try:
            artifacts = step_func(self.spec, self.project_dir, **kwargs)
            success = True
        except Exception as e:
            error = str(e)

        result = StepResult(
            step=step,
            success=success,
            error=error,
            artifacts=artifacts,
        )
        self.state.step_results[step] = result
        self._save_state()
        return result

    def run_pipeline(self, steps: Optional[List[str]] = None) -> Dict[str, StepResult]:
        """
        Run the full pipeline (or a subset of steps).

        Args:
            steps: list of step names to run. Defaults to all 5 steps.

        Returns:
            Dict of step_name -> StepResult
        """
        if steps is None:
            steps = AVAILABLE_STEPS

        step_functions = {
            "generate_schematic": self._import_step("step1_generate_schematic", "run"),
            "component_selection": self._import_step("step2_component_selection", "run"),
            "pcb_layout": self._import_step("step3_pcb_layout", "run"),
            "validation": self._import_step("step4_validation", "run"),
            "manufacturing": self._import_step("step5_manufacturing", "run"),
        }

        for step in steps:
            if step not in step_functions:
                continue
            if not self.should_run_step(step):
                continue  # Skip already-completed steps

            step_func = step_functions[step]
            if step_func is None:
                # Module not yet implemented
                result = StepResult(step=step, success=False, error=f"Step '{step}' not yet implemented")
                self.state.step_results[step] = result
                self._save_state()
                continue

            result = self.execute_step(step, step_func)
            if not result.success and not self._is_recoverable(result.error):
                break  # Stop on non-recoverable errors

        return self.state.step_results

    def _import_step(self, module_name: str, func_name: str):
        """Lazy-import a step module to avoid import errors if not yet created."""
        try:
            module = __import__(f"engine.{module_name}", fromlist=[func_name])
            return getattr(module, func_name, None)
        except ImportError:
            return None

    def _is_recoverable(self, error: Optional[str]) -> bool:
        """Determine if an error is recoverable (pipeline should continue)."""
        if not error:
            return True
        # DRC violations are recoverable (user can fix in GUI)
        if "DRC" in error or "violation" in error.lower():
            return True
        # Missing API keys are recoverable
        if "API" in error or "key" in error.lower() or "credential" in error.lower():
            return True
        # Not found errors are non-recoverable
        if "not found" in error.lower() or "not installed" in error.lower():
            return False
        return True

    def get_summary(self) -> Dict[str, Any]:
        """Get a human-readable summary of pipeline state."""
        return {
            "project": self.state.project_name,
            "project_dir": self.project_dir,
            "current_step": self.state.current_step,
            "steps_completed": sum(1 for r in self.state.step_results.values() if r.success),
            "steps_failed": sum(1 for r in self.state.step_results.values() if not r.success),
            "step_details": {
                step: {"success": r.success, "error": r.error, "artifacts": r.artifacts}
                for step, r in self.state.step_results.items()
            },
        }
