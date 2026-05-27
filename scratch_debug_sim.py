import pickle
# We can't use pickle easily since it wasn't saved.
# Let's inspect the code or run a quick debug script that prints the timeline of states for Water.

import sys
sys.path.append('scratch')
from dispenser_simulation import run_simulation, liquids

liq = liquids[0] # Water
time_log, actual_mass, reported_mass, motor_speed, state_log, learned_spg = run_simulation(liq)

# Let's print out every state change event
print("Water State Timeline:")
current_state = None
for t, state, act, rep in zip(time_log, state_log, actual_mass, reported_mass):
    if state != current_state:
        print(f"Time {t:.3f}s: State changed to {state} | Actual: {act:.4f}g, Reported: {rep:.2f}g")
        current_state = state
print(f"End Time {time_log[-1]:.3f}s: Final State {state_log[-1]} | Actual: {actual_mass[-1]:.4f}g, Reported: {reported_mass[-1]:.2f}g")
