# CSV Recipe Feature Plan

## Context

The dashboard currently accepts pump targets via inline number inputs on the Recipe screen, with no persistence between sessions. The user wants to load recipes from CSV files and save them to the browser's localStorage for reuse across sessions. This is a dashboard-only feature — no firmware changes needed.

## CSV Format

```csv
Name,Description
My Chemical Mix,A test recipe

Pump,Target_g,Viscosity,Chemical,Notes
1,10.00,low,Deionized Water,Base solvent
2,5.00,high,Glycerol,Viscosity agent
3,0.00,low,,Skipped pump
```

- `Name,` and `Description,` lines define recipe metadata (before first empty line).
- Data rows: Pump (1-7), Target_g (float >= 0), Viscosity (low/high), Chemical (string), Notes (string).
- Pump 0 or target 0 = skip. Missing chemical/notes = empty string.

## Only File Modified

`dashboard/index.html` — all changes are additions (new CSS, HTML, JS). No existing code is deleted or broken.

## Implementation Steps

### 1. Extend pump data model (JS, ~line 200)

Add `chemical` and `notes` properties to each pump object. These are display-only metadata, never sent to firmware.

### 2. Add CSV parser and validator (JS, new functions before `buildStaticUi`)

- **`parseCsvRow(line)`** — character-by-character parser handling quoted fields (commas inside quotes, escaped double-quotes).
- **`parseCsvContent(text)`** — strips BOM, splits at empty line for metadata vs data rows, parses data rows into `{pump, target, viscosity, chemical, notes}` objects. Handles `\r\n` and `\n`.
- **`validateCsvRows(rows)`** — validates pump 1-7, target >= 0, viscosity low/high. Returns error strings.
- **`loadPumpsFromCsv(rows)`** — maps CSV rows to the `pumps` array, resets run state.
- **`onCsvFileSelected(e)`** — FileReader handler for the file input.

### 3. Add recipe library management (JS, new functions)

- **Storage**: localStorage key `grav_recipes_v1`. Schema: array of `{id, name, description, createdAt, pumps: [{pump, target, viscosity, chemical, notes}]}`.
- **`getRecipes()` / `saveRecipes(recipes)`** — localStorage helpers.
- **`saveRecipeToLibrary()`** — saves current pump state to library.
- **`loadRecipeFromLibrary(id)`** — loads a recipe into pump targets.
- **`deleteRecipe(id)`** — removes from library.
- **`exportRecipeAsCsv()`** — generates CSV from current pumps, triggers download via Blob/URL.createObjectURL.

### 4. Add recipe library render (JS)

- **`renderRecipeLibrary()`** — populates `#libraryList` with recipe cards showing name, description, active pumps, date, LOAD/DELETE buttons.

### 5. Add recipe metadata variables (JS, ~line 198)

```js
let recipeName='', recipeDesc='';
```

### 6. Wire up event handlers (JS, at line ~302)

- File input `onchange` -> `onCsvFileSelected`
- Drag-and-drop on recipe panel -> same CSV parse flow
- Tab buttons -> toggle editor/library visibility
- Save/Export/Clear buttons -> library functions

### 7. Update `buildStaticUi` (JS, ~line 216)

Add chemical display (p.chemical) and notes display (p.notes) to each pump-edit card in the recipe editor tab.

### 8. Update HTML: Recipe screen (HTML, ~line 88)

Replace the current Recipe screen with a tabbed layout:
- **Tab bar**: EDITOR | LIBRARY buttons
- **Editor tab**: Existing pump targets + summary panel (unchanged logic)
- **Library tab**: Saved recipes list + info panel
- **New controls**: "LOAD CSV FILE" button (hidden file input), "SAVE TO LIBRARY", "EXPORT CSV"

### 9. Add CSS (CSS, before `</style>`)

```css
.recipe-tabs { display:flex; gap:4px; margin-bottom:6px }
.recipe-tabs button { min-height:28px; font-size:10px; padding:4px 12px; flex:none }
.recipe-card { background:#0a1728; border:1px solid #1d3450; border-radius:12px; padding:10px; margin-bottom:6px }
.recipe-card-header { display:flex; justify-content:space-between; align-items:center; font-size:13px; font-weight:900 }
```

## Edge Cases

- **Windows line endings**: Handled by `split(/\r?\n/)`
- **UTF-8 BOM**: Stripped by `replace(/^﻿/,'')`
- **Commas in chemical names**: Handled by quoted-field parser
- **More/fewer columns**: Extra ignored, missing defaults to `""`, rows < 3 cols skipped
- **localStorage quota**: ~500 bytes/recipe, well within 5-10MB browser limits
- **Duplicate names**: Not deduplicated — each save is a new entry with unique ID

## Verification

1. Open `dashboard/index.html` in Chrome/Edge.
2. Click "LOAD CSV FILE" and select a test CSV — pumps populate correctly.
3. Drag-and-drop a CSV onto the recipe panel — same result.
4. Click "SAVE TO LIBRARY" — verify `grav_recipes_v1` in browser DevTools localStorage.
5. Switch to LIBRARY tab — see saved recipe, click LOAD — pumps populate.
6. Click "EXPORT CSV" — file downloads with correct format.
7. Reload page — saved recipes persist, current pump targets reset (intended).
8. Verify existing features still work: simulator mode, Web Serial connection, calibration, run sequence.
9. Test CSV with quoted commas: `1,5.0,low,"Sulfuric Acid, Conc.",Notes` — parses correctly.
10. Test validation: pump 8, negative target, invalid viscosity — errors logged in red console.
