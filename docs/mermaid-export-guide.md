# Mermaid Diagram Export Guide

## Option A: Export via Mermaid CLI
1. Install Node.js.
2. Install Mermaid CLI globally:
   `npm install -g @mermaid-js/mermaid-cli`
3. Run export commands from project root:

```bash
mmdc -i diagrams/system-flowchart.mmd -o diagrams/system-flowchart.png
mmdc -i diagrams/charging-control-logic.mmd -o diagrams/charging-control-logic.png
mmdc -i diagrams/protection-mechanism.mmd -o diagrams/protection-mechanism.png
mmdc -i diagrams/system-block-diagram.mmd -o diagrams/system-block-diagram.png
```

## Option B: GitHub Native Rendering
1. Open `.mmd` files directly in GitHub.
2. GitHub renders Mermaid automatically in markdown fenced blocks.
3. To include rendered diagrams in reports, paste Mermaid blocks into markdown and export PDF from browser/VS Code.

## Option C: Mermaid Live Editor
1. Open https://mermaid.live
2. Paste content from `.mmd` file.
3. Export SVG/PNG.
