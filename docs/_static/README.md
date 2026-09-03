# DR_EVT Logo and Static Assets

## Logo Files

### `dr_evt_logo.svg` (64x64)
Main logo for light themes. Pixelated clock icon representing discrete event-driven simulation.

**Colors:**
- Border: `#2c3e50` (dark blue-gray)
- Clock face: `#3498db` (bright blue)
- Clock hands: `#e74c3c` (red)

**Usage:** README badges, documentation headers, website

### `dr_evt_logo_dark.svg` (64x64)
Dark theme variant with inverted colors.

**Colors:**
- Border: `#ecf0f1` (light gray)
- Clock face: `#2980b9` (darker blue)
- Clock hands: `#e67e22` (orange)

**Usage:** Dark mode documentation, GitHub social preview

### `favicon.ico` (32x32)
Simplified favicon version for browser tabs.

**Usage:** Website favicon, bookmark icon

## Design Rationale

The pixelated clock represents:
- **Discrete events** - Pixelated aesthetic emphasizes discrete time steps
- **Time-driven simulation** - Clock symbolizes time advancement
- **HPC scheduling** - Clock hands at 10:10 position (classic watch photography pose)

## Color Palette

| Color | Hex | Usage |
|-------|-----|-------|
| Deep Blue | `#2c3e50` | Primary (borders, text) |
| Bright Blue | `#3498db` | Accent (clock face) |
| Red | `#e74c3c` | Highlight (clock hands) |
| Light Gray | `#ecf0f1` | Dark theme borders |
| Dark Blue | `#2980b9` | Dark theme face |
| Orange | `#e67e22` | Dark theme hands |

## Integration Examples

### Sphinx Documentation (docs/conf.py)
```python
html_logo = '_static/dr_evt_logo.svg'
html_favicon = '_static/favicon.ico'
```

### README.md Badge
```markdown
![DR_EVT Logo](docs/_static/dr_evt_logo.svg)
```

### GitHub Social Preview
Upload `dr_evt_logo.svg` (or 1200x630 PNG export) to:
Settings → Social Preview → Upload image

## File Formats

All logos provided as **SVG** (Scalable Vector Graphics):
- ✅ Scales to any size without quality loss
- ✅ Small file size (~2-3 KB)
- ✅ Editable in any vector graphics tool
- ✅ Renders perfectly in modern browsers

## Editing

To modify the logo:
1. Open SVG in vector editor (Inkscape, Figma, Adobe Illustrator)
2. Each pixel is a `<rect>` element with x, y, width, height
3. Change `fill` attribute to adjust colors
4. Export as SVG or PNG at desired size

## License

Same license as DR_EVT project (MIT License).
