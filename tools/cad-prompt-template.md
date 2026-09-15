# Schematic → Parametric OpenSCAD Prompt Template

Reusable prompt for turning a schematic drawing into a parametric, printable
OpenSCAD model. Works with any strong vision+code model (Claude Sonnet, Gemini
Pro, GPT-4o). Paste the template below, attach the schematic image, and fill in
the bracketed `[...]` placeholders.

---

## Template

```
You are a mechanical CAD engineer. Convert the attached schematic drawing into
a parametric OpenSCAD model for 3D printing.

## Input
- Schematic: [attached image]
- Part purpose: [e.g. "bracket holding two FHL-LD19P lidars on a 25 mm mast"]
- Critical dimensions (do NOT change these): [e.g. "lidar dia 45 mm, mast dia
  25 mm, tilt angle 25 deg"]
- Print method: [FDM / resin / SLA]
- Material: [e.g. PETG, PLA, resin]

## Output requirements
1. Generate a SINGLE .scad file with ALL dimensions as named parameters at the
   top (e.g. `lidar_dia = 45;`), so I can edit values and re-render without
   redesigning.
2. State every assumption you made about ambiguous features: line weights,
   hidden lines, tolerances, clearances, wall thickness, fillets.
3. Flag any dimension in the drawing you could NOT read confidently.
4. Use clean boolean operations (difference/intersection) and avoid
   non-manifold geometry where possible.
5. Add a comment header listing: units (mm), the parameters, and the render
   command (`openscad -o out.stl in.scad`).

## Constraints
- Do NOT invent precision-critical values (press fits, threads, tolerances).
  Leave them as parameters and tell me what to verify.
- Prefer parametric modules over hardcoded geometry.
- Keep the model printable: no overhangs that need supports unless noted.
```

---

## Usage notes

- **Attach the schematic as an image** — the model needs the actual drawing, not
  a description.
- **List critical dimensions explicitly** — this is the single most important
  line. The model will respect these and only guess the rest.
- **Iterate**: render the output, feed the result back, and ask for fixes. Treat
  the first output as a draft.
- **Verify precision parts yourself** — the model can misread a dimension or
  invent a tolerance. The parametric approach lets you fix the critical numbers
  and let the model handle structure.

## Model choice (OpenRouter)

| Model | Use when |
| --- | --- |
| Claude 3.7 Sonnet | Default — best overall vision+code+geometry |
| Gemini 2.5 Pro | Hand-drawn / messy schematics (best raw vision) |
| GPT-4o / 4.1 | Strong alternative |
| DeepSeek-V3/R1 | Budget, but weaker vision — avoid for complex drawings |