# W2C to MW 2.0.0 Converter RC6

This update follows RC5. The required source release remains **W2C 2.0.0**. Rebuild your world with this converter to include the changes below.

## New optional components

- **W2C provisional access to unopened roads Addon** includes the extra roads and placement changes used in the Sandbox. Installation data is prepared for NORMAL, WORLD or X360 and for the selected Carbon resources. These unfinished roads remain provisional.
- **LBx64 W2CtoMW RC3.1** includes expanded vehicle and vehicle-resource limits, a 64-bit resource host, and components for DLSS and DLAA on compatible systems. Select its installation in the converter. DLSS and DLAA require a supported NVIDIA GPU, driver and SunSet.
- Unselected components are still saved with installation guides in the converted pack's `Optional` folder. Installing the pack also places them in `W2CMWPublic/Optional`.

## Changes since RC5

- Improved daytime headlights in shade, daytime tunnel lighting, and suppression of precipitation inside tunnels.
- Fixed invisible winter terrain collision remaining on the Rockport golf course and the collision that could lift a car near the bus terminal.
- Incorporated the Sandbox updates to tree and building animations, world loading and rendering.
- Ordinary terrain, lighting, precipitation and road fixes apply with the Addon disabled. Only provisional access to unopened roads depends on the Addon option.
- Installing LBx64 retires the old LB and duplicate LB modules from loader search paths. The old files and existing settings are protected for restoration by the included rollback utility.
- Upgrading an RC5 or earlier installation preserves the original world backups made before W2C was first installed. They are not replaced with the previously installed W2C world.

## Updating and compatibility

1. Close the game and launcher, then rebuild from your W2C 2.0.0 source with Converter RC6. You can select an existing W2C game folder.
2. Choose the Addon and LBx64 installation options, review the checks, and install.
3. To restore the original environment, close the game and run `W2CMWPublic/W2CMW-Rollback.exe`. Saved games, vehicle files, attribute data and control bindings are outside this update.

LBx64 RC3.1 supports the compatible EN-US 1.3 executable. Use the base W2C converter without automatic LBx64 installation for the Redux executable. Other rendering wrappers may require separate configuration.

On the development PC's RTX 2070 SUPER with driver 457.85, the bundled component reported DLSS unavailable. DLSS and DLAA are not verified on that combination; a compatible driver and further runtime testing are needed.

Known issues remain: some stock MW events have checkpoints that cannot be crossed normally, and loading can occasionally take a long time. Compatibility with every other mod combination is not guaranteed.

**Redistribution of converted output is prohibited.** Share the converter and original-source distribution pages instead.
