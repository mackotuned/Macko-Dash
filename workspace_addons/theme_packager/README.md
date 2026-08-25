# MackoDash Theme Builder

The Theme Builder workflow inside `MackoDashUtility.exe` converts a one-screen
SquareLine Studio C export into a validated `.mdtheme.zip` package for
`/MACKODASH/THEMES` on the dashboard SD card.

## Customer workflow

1. Create a 1024x600, LVGL 8.x SquareLine project.
2. Follow `../references/MackoDash_SquareLine_Customer_Instructions.txt` for
  the complete Label, Bar, Arc, unit, and settings-button name lists.
3. Export the complete C project and ZIP the exported folder.
4. Open MackoDash Utility, choose **Build a Theme**, and select that ZIP.
5. Enter a theme name and unique lowercase theme ID, then select **Build Theme**.
6. Select **Preview Theme** to inspect the dashboard with typical and longest simulated values.
7. Use **Copy to SD Card** and select the SD card drive.

The preview highlights objects that extend beyond the canvas or may clip their
longest live value. Its checks also report unsupported controls, unresolved
`dash_` binding names, and missing Settings or REC buttons before installation.

Output location, canvas overrides, font substitution, and the conversion report
are available under **Show technical details**. Most customers do not need them.

Strict mode is the customer default. It stops on custom generated fonts because
SquareLine exports compiled LVGL font C files, not the original TTF/OTF files.
This prevents a package from silently changing typography. The substitution
checkbox is only for development previews. Built-in Montserrat 12, 14, 28, and
44 pass strict validation because the dashboard contains exact matches.

## Supported fidelity fields

- SquareLine anchor, x/y offset, z-order, fixed/content dimensions
- Labels, bars, arcs, images, and buttons
- Bar ranges, colors, opacity, radius, rotation, and indicator images
- Raw RGB565 and RGB565A8 images, including SquareLine image zoom
- A `conversion-report.json` stored in every package

Unsupported widgets, malformed images, multiple screens, invalid metadata, and
missing exact fonts stop conversion instead of creating an approximate customer
package.

## Run from source

```powershell
& 'C:/Users/mackb/.espressif/python_env/idf5.5_py3.11_env/Scripts/python.exe' `
  workspace_addons/theme_packager/mackodash_theme_builder.py
```

The command-line converter is `convert_squareline_export.py`. Run it with
`--help` for automation options.

The customer executable is `MackoDashUtility.exe`. There is no separate Theme
Builder download.